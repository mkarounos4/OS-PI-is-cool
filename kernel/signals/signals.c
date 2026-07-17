#include "signals.h"
#include <stddef.h>
#include "scheduler/scheduler.h"
#include "scheduler/process.h"
#include "errno.h"
#include "threading/thread.h"

#define USER_SIG_DFL ((void (*)(int))0)
#define USER_SIG_IGN ((void (*)(int))1)

void (*def_signal_handlers[32])(int);

void SIG_IGN(int signum) {
    (void)signum;
}

void SIG_CONT(int signum) {
    (void)signum;
    tcb_t *tcb = get_curr_thread();
    if (tcb != NULL) {
        continue_thread(tcb);
    }
}

void SIG_STOP(int signum) {
    (void)signum;
    tcb_t *tcb = get_curr_thread();
    if (tcb != NULL) {
        stop_thread(tcb);
    }
}

void SIG_TERM(int signum) {
    (void)signum;
    tcb_t *tcb = get_curr_thread();
    if (tcb != NULL) {
        terminate_thread(tcb);
    }
}

void SIG_DFL(int signum) {
    def_signal_handlers[signum](signum);
}

void SIG_NOT_IMPLEMENTED(int signum) {
    (void)signum;
    fatal_exception("ERROR: tried to send unimplemented signal.");
}

void user_def_sig_handler(int signum) {
    pcb_t *pcb = get_curr_process();
    signalset_t old_mask = 0;
    signalset_t new_mask = pcb->sigactions[signum].sa_mask | (1 << signum);
    sigprocmask(SIG_BLOCK, &new_mask, &old_mask);
    pcb->sigactions[signum].sa_handler(signum);
    sigprocmask(SIG_SET, &old_mask, NULL);
}

void initialize_signals() {
    for (int i = 0; i < 32; i++) {
        def_signal_handlers[i] = SIG_NOT_IMPLEMENTED;
    }

    def_signal_handlers[SIGCONT] = SIG_CONT;
    def_signal_handlers[SIGSTOP] = SIG_STOP;
    def_signal_handlers[SIGTSTP] = SIG_STOP;
    def_signal_handlers[SIGTTIN] = SIG_STOP;
    def_signal_handlers[SIGTTOU] = SIG_STOP;
    def_signal_handlers[SIGINT] = SIG_TERM;
    def_signal_handlers[SIGKILL] = SIG_TERM;
    def_signal_handlers[SIGTERM] = SIG_TERM;
    def_signal_handlers[SIGCHLD] = SIG_IGN;
}

static void handle_interruptable_signal(tcb_t *tcb, int signum) {
    pcb_t *pcb = tcb->pcb;
    int terminating = pcb->sigactions[signum].sa_handler == SIG_TERM;
    int continuing = pcb->sigactions[signum].sa_handler == SIG_CONT;

    if (signum == SIGKILL) {
        terminate_thread(tcb);
        return;
    }

    if (tcb->state == THREAD_BLOCKED_INTERRUPTABLE) {
        if (!continuing) {
            unblock_thread(tcb);
            return;
        }
    } else if (tcb->state == THREAD_BLOCKED_KILLABLE) {
        if (terminating) {
            terminate_thread(tcb);
        }
    } else if (tcb->state == THREAD_STOPPED) {
        if (continuing) {
            continue_thread(tcb);
            return;
        }
    }
}


int s_kill(pid_t pid, int signal) {
    if (signal < 0 || signal >= 32) {
        return (int)SYS_EINVAL;
    }

    if (pid < 0) {
        pgrp_t *pgrp = get_pgrp_by_pgid(-pid);
        int sent = 0;

        if (pgrp == NULL) {
            return (int)SYS_ESRCH;
        }

        for (size_t i = 0; i < vec_len(&pgrp->pids); i++) {
            pid_t member_pid = (pid_t)(uintptr_t)vec_get(&pgrp->pids, i);
            if (s_kill(member_pid, signal) == 0) {
                sent = 1;
            }
        }

        return sent ? 0 : (int)SYS_ESRCH;
    }

    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    void (*handler)(int) = pcb->sigactions[signal].sa_handler;
    void (*default_handler)(int) = handler == SIG_DFL ? def_signal_handlers[signal] : handler;

    // If cont/stop/term, send to all threads.
    if (default_handler == SIG_STOP || default_handler == SIG_CONT ||
        default_handler == SIG_TERM) {
        int sent = 0;

        for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
            tid_t member_tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
            if (pthread_kill(member_tid, signal) == 0) {
                sent = 1;
            }
        }

        return sent ? 0 : (int)SYS_ESRCH;
    }

    if (default_handler == SIG_IGN) {
        return 0;
    }

    // otherwise send process wide (only one thread).
    for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
        tid_t member_tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
        tcb_t *tcb = thread_get_by_tid(member_tid);
        if (tcb == NULL || (tcb->mask & (1 << signal))) {
            continue;
        }

        tcb->pending_signals |= (1 << signal);
        handle_interruptable_signal(tcb, signal);
        return 0;
    }

    pcb->pending_signals |= (1 << signal);
    return 0;
}

int pthread_kill(tid_t tid, int signal) {
    tcb_t *tcb = thread_get_by_tid(tid);
    if (tcb == NULL) {
        return (int)SYS_EINVAL;
    }
    pcb_t *pcb = tcb->pcb;

    if (tcb == get_curr_thread() && !(tcb->mask & (1 << signal))) {
        pcb->sigactions[signal].sa_handler(signal);
        return 0;
    }

    if (signal == SIGCONT) {
        int curr = 0;
        while (pcb->pending_signals >> curr) {
            if ((pcb->pending_signals & (1 << curr)) && pcb->sigactions[curr].sa_handler == SIG_STOP) {
                pcb->pending_signals &= ~(1 << curr);
            }
            curr++;
        }
    }

    tcb->pending_signals |= (1 << signal);
    if (!(tcb->mask & (1 << signal))) {
        int unblocked = send_unblock_event(tcb->tid, BLOCK_UNTIL_SIGNAL);
        if (!unblocked || tcb->state == THREAD_BLOCKED_INTERRUPTABLE ||
            tcb->state == THREAD_BLOCKED_KILLABLE || tcb->state == THREAD_STOPPED) {
            handle_interruptable_signal(tcb, signal);
        }
    }


    return 0;
}

long send_sigchld(pid_t child) {
    pcb_t *child_pcb = get_pcb_by_pid(child);
    if (child_pcb == NULL) {
        return SYS_ESRCH;
    }

    if (child_pcb->state == PROC_STOPPED_STATE) {
        child_pcb->wait_stop_pending = 1;
    } else if (child_pcb->state == PROC_RUNNING_STATE) {
        child_pcb->wait_cont_pending = 1;
    }

    pid_t parent_pid = child_pcb->ppid;
    pcb_t* parent_pcb = get_pcb_by_pid(parent_pid);
    if (parent_pcb == NULL) {
        return SYS_ESRCH;
    }

    for (size_t i = 0; i < vec_len(&parent_pcb->child_waitq); i++) {
        tid_t next_thread = (tid_t)(uintptr_t)vec_get(&parent_pcb->child_waitq, i);
        tcb_t *tcb = thread_get_by_tid(next_thread);
        if (tcb == NULL) {
            continue;
        }
        if (tcb->waiting_for_pid == -1 ||
            tcb->waiting_for_pid == child ||
            (tcb->waiting_for_pid < -1 && child_pcb->pgid == -tcb->waiting_for_pid)) {
            if (child_pcb->state == PROC_ZOMBIE_STATE ||
                (child_pcb->state == PROC_STOPPED_STATE && (tcb->waiting_for_flags & WUNTRACED)) ||
                ((child_pcb->state == PROC_RUNNING_STATE) &&
                 (tcb->waiting_for_flags & WCONTINUED))) {
                unblock_thread(tcb);
            }
            return 0;
        }
    }

    return 0;
}

int sigprocmask(int how, const signalset_t *set, signalset_t *oldset) {
    if (set == NULL) {
        return (int)SYS_EFAULT;
    }

    tcb_t *tcb = get_curr_thread();
    if (tcb == NULL) {
        return (int)SYS_ESRCH;
    }

    if (oldset != NULL) {
        *oldset = tcb->mask;
    }

    if (how == SIG_BLOCK) {
        tcb->mask |= *set;
    } else if (how == SIG_UNBLOCK) {
        tcb->mask &= ~(*set);
    } else if (how == SIG_SET) {
        tcb->mask = *set;
    } else {
        return (int)SYS_EINVAL;
    }

    return 0;
}

int sigemptyset(signalset_t *set) {
    if (set == NULL) {
        return (int)SYS_EFAULT;
    }

    *set = 0;
    return 0;
}

int sigaddset(signalset_t *set, int signum) {
    if (set == NULL) {
        return (int)SYS_EFAULT;
    }
    if (signum < 0 || signum >= 32) {
        return (int)SYS_EINVAL;
    }

    *set |= (1 << signum);
    return 0;
}

int sigfillset(signalset_t *set) {
    if (set == NULL) {
        return (int)SYS_EFAULT;
    }

    *set = 0xFFFFFFFF;
    return 0;
}

int sigsuspend(const signalset_t *mask) {
    tcb_t *tcb = get_curr_thread();
    if (mask == NULL) {
        return (int)SYS_EFAULT;
    }
    if (tcb == NULL) {
        return (int)SYS_ESRCH;
    }

    signalset_t old_set = 0;
    sigprocmask(SIG_SETMASK, mask, &old_set);
    tcb->blocked_until |= (1 << BLOCK_UNTIL_SIGNAL);
    block_thread(tcb, THREAD_BLOCKED_INTERRUPTABLE);
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    return (int)SYS_EINTR;
}

int sigaction(int signum, struct sigaction *sa, struct sigaction *old) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return (int)SYS_ESRCH;
    }

    if (signum < 0 || signum >= 32) {
        return (int)SYS_EINVAL;
    }

    if (sa == NULL) {
        return (int)SYS_EFAULT;
    }

    if (old != NULL) {
        *old = pcb->sigactions[signum];
    }

    if ((signum == SIGKILL || signum == SIGSTOP) &&
        sa->sa_handler != USER_SIG_DFL &&
        sa->sa_handler != SIG_DFL) {
        return (int)SYS_EINVAL;
    }

    pcb->sigactions[signum] = *sa;
    if (sa->sa_handler == USER_SIG_DFL) {
        pcb->sigactions[signum].sa_handler = SIG_DFL;
    } else if (sa->sa_handler == USER_SIG_IGN) {
        pcb->sigactions[signum].sa_handler = SIG_IGN;
    }
    return 0;
}
