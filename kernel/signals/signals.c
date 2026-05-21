#include "signals.h"
#include "scheduler/process.h"

long s_kill(pid_t pid, int signal) {
    pcb_t *pcb = get_pcb_by_pid(pid);
    if (pcb == NULL) {
        return -1;
    }

    if (pcb->state == PROC_BLOCKED_STATE) {
        // add to signal queue
        return 0;
    }

    if (signal == SIGKILL) {
        terminate_process(pcb);
    } else if (signal == SIGSTOP) {
        stop_process(pcb);
    } else if (signal == SIGCONT) {
        continue_process(pcb);
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
