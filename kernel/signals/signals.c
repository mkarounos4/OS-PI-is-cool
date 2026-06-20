#include "signals.h"
#include "scheduler/scheduler.h"
#include "scheduler/process.h"

void (*def_signal_handlers[32])(int);

void SIG_IGN(int signum) {
    (void)signum;
}

void SIG_CONT(int signum) {
    (void)signum;
    pcb_t *pcb = get_curr_process();
    if (pcb != NULL) {
        continue_process(pcb);
    }
}

void SIG_STOP(int signum) {
    (void)signum;
    pcb_t *pcb = get_curr_process();
    if (pcb != NULL) {
        stop_process(pcb);
    }
}

void SIG_TERM(int signum) {
    (void)signum;
    pcb_t *pcb = get_curr_process();
    if (pcb != NULL) {
        terminate_process(pcb);
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
    signalset_t old_mask;
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

int s_kill(pid_t pid, int signal) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return -1;
    }
    if (pcb == get_curr_process() && !(pcb->mask & (1 << signal))) {
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

    pcb->pending_signals |= (1 << signal);
    if (!(pcb->mask & (1 << signal))) {
        send_unblock_event(pcb->pid, BLOCK_UNTIL_SIGNAL);
    }

    return 0;
}

long send_sigchld(pid_t child) {
    pcb_t *child_pcb = get_pcb_by_pid(child);
    if (child_pcb == NULL) {
        return -1;
    }

    if (child_pcb->state == PROC_STOPPED_STATE) {
        child_pcb->wait_stop_pending = 1;
    } else if (child_pcb->state == PROC_READY_STATE || child_pcb->state == PROC_RUNNING_STATE) {
        child_pcb->wait_cont_pending = 1;
    }

    pid_t parent_pid = child_pcb->ppid;
    pcb_t* parent_pcb = get_pcb_by_pid(parent_pid);
    if (parent_pcb == NULL) {
        return -1;
    }

    if (parent_pcb->waiting_for_pid == -1 || parent_pcb->waiting_for_pid == child) {
        if (child_pcb->state == PROC_ZOMBIE_STATE ||
            (child_pcb->state == PROC_STOPPED_STATE && (parent_pcb->waiting_for_flags & WUNTRACED)) ||
            ((child_pcb->state == PROC_RUNNING_STATE || child_pcb->state == PROC_READY_STATE) &&
             (parent_pcb->waiting_for_flags & WCONTINUED))) {
            unblock_process(parent_pcb);
        }
        return 0;
    }

    return 0;
}

int sigprocmask(int how, const signalset_t *set, signalset_t *oldset) {
    if (set == NULL) {
        return -2;
    }

    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (oldset != NULL) {
        *oldset = pcb->mask;
    }

    if (how == SIG_BLOCK) {
        pcb->mask |= *set;
    } else if (how == SIG_UNBLOCK) {
        pcb->mask &= ~(*set);
    } else if (how == SIG_SET) {
        pcb->mask = *set;
    } else {
        return -2;
    }

    return 0;
}

int sigemptyset(signalset_t *set) {
    if (set == NULL) {
        return -1;
    }

    *set = 0;
    return 0;
}

int sigaddset(signalset_t *set, int signum) {
    if (set == NULL) {
        return -1;
    }

    *set |= (1 << signum);
    return 0;
}

int sigfillset(signalset_t *set) {
    if (set == NULL) {
        return -1;
    }

    *set = 0xFFFFFFFF;
    return 0;
}

int sigsuspend(const signalset_t *mask) {
    pcb_t *pcb = get_curr_process();
    if (mask == NULL) {
        return -2;
    }
    if (pcb == NULL) {
        return -1;
    }

    signalset_t old_set;
    sigprocmask(SIG_SETMASK, mask, &old_set);
    pcb->blocked_until |= (1 << BLOCK_UNTIL_SIGNAL);
    block_process(pcb);
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    return -1;
}

int sigaction(int signum, struct sigaction *sa, struct sigaction *old) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    if (sa == NULL) {
        return -1;
    }

    if (old != NULL) {
        *old = pcb->sigactions[signum];
    }

    pcb->sigactions[signum] = *sa;
    return 0;
}
