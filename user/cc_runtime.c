#define fork cc_inline_fork
#define dup2 cc_inline_dup2
#define setpgid cc_inline_setpgid
#define getpgrp cc_inline_getpgrp
#define tcsetpgrp cc_inline_tcsetpgrp
#define mount cc_inline_mount
#define unmount cc_inline_unmount
#define pipe cc_inline_pipe
#define ps cc_inline_ps
#define exec cc_inline_exec
#define proc_change_priority cc_inline_proc_change_priority
#include "lib/syscall.h"
#undef fork
#undef dup2
#undef setpgid
#undef getpgrp
#undef tcsetpgrp
#undef mount
#undef unmount
#undef pipe
#undef ps
#undef exec
#undef proc_change_priority

#include <stdint.h>

void __cc_runtime_entry(void) {
}

long syscall0(long nr) {
    return sys_call0(nr);
}

long syscall1(long nr, long a0) {
    return sys_call1(nr, a0);
}

long syscall2(long nr, long a0, long a1) {
    return sys_call2(nr, a0, a1);
}

long syscall3(long nr, long a0, long a1, long a2) {
    return sys_call3(nr, a0, a1, a2);
}

long syscall4(long nr, long a0, long a1, long a2, long a3) {
    return sys_call4(nr, a0, a1, a2, a3);
}

long syscall5(long nr, long a0, long a1, long a2, long a3, long a4) {
    return sys_call5(nr, a0, a1, a2, a3, a4);
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
    return sys_call6(nr, a0, a1, a2, a3, a4, a5);
}

long yield(void) {
    return sys_call0(S_YIELD);
}

long current_el(void) {
    return sys_call0(S_CURRENT_EL);
}

long touch(char **file_paths) {
    return sys_call1(S_FS_TOUCH, (long)(uintptr_t)file_paths);
}

long mv(char *src_path, char *dest_path) {
    return sys_call2(S_FS_MV, (long)(uintptr_t)src_path,
                     (long)(uintptr_t)dest_path);
}

long rm(char **file_paths) {
    return sys_call1(S_FS_RM, (long)(uintptr_t)file_paths);
}

long cat(char **files, char *output_file, int flag) {
    return sys_call3(S_FS_CAT, (long)(uintptr_t)files,
                     (long)(uintptr_t)output_file, flag);
}

long cp(char *src_path, char *dest_path, int flag) {
    return sys_call3(S_FS_CP, (long)(uintptr_t)src_path,
                     (long)(uintptr_t)dest_path, flag);
}

long ls(char *dir_path) {
    return sys_call1(S_FS_LS, (long)(uintptr_t)dir_path);
}

long fs_mkdir(char **file_paths) {
    return sys_call1(S_FS_MKDIR, (long)(uintptr_t)file_paths);
}

long cd(char *path) {
    return sys_call1(S_FS_CD, (long)(uintptr_t)path);
}

int open(const char *fname, int mode) {
    return (int)sys_call2(S_FS_OPEN, (long)(uintptr_t)fname, mode);
}

int close(int fd) {
    return (int)sys_call1(S_FS_CLOSE, fd);
}

int lseek(int fd, int offset, int whence) {
    return (int)sys_call3(S_FS_LSEEK, fd, offset, whence);
}

int read(int fd, char *buf, int n) {
    return (int)sys_call3(S_FS_READ, fd, (long)(uintptr_t)buf, n);
}

int write(int fd, const char *buf, int n) {
    return (int)sys_call3(S_FS_WRITE, fd, (long)(uintptr_t)buf, n);
}

int fs_chmod(char *file_name, char *new_perms, int flag) {
    return (int)sys_call3(S_FS_CHMOD,
                          (long)(uintptr_t)file_name,
                          (long)(uintptr_t)new_perms,
                          flag);
}

long sigprocmask(int how, const void *set, void *oldset) {
    return sys_call3(S_SIGPROCMASK, how, (long)(uintptr_t)set,
                     (long)(uintptr_t)oldset);
}

long sigemptyset(void *set) {
    return sys_call1(S_SIGEMPTYSET, (long)(uintptr_t)set);
}

long sigaddset(void *set, int signum) {
    return sys_call2(S_SIGADDSET, (long)(uintptr_t)set, signum);
}

long sigfillset(void *set) {
    return sys_call1(S_SIGFILLSET, (long)(uintptr_t)set);
}

long sigsuspend(const void *mask) {
    return sys_call1(S_SIGSUSPEND, (long)(uintptr_t)mask);
}

long sigaction(int signum, void *action, void *old_action) {
    return sys_call3(S_SIGACTION, signum, (long)(uintptr_t)action,
                     (long)(uintptr_t)old_action);
}

long fork(void) {
    return sys_call0(S_FORK);
}

long dup2(int oldfd, int newfd) {
    return sys_call2(S_DUP2, oldfd, newfd);
}

long setpgid(int pid, int pgid) {
    return sys_call2(S_SETPGID, pid, pgid);
}

long getpgrp(void) {
    return sys_call0(S_GETPGRP);
}

long tcsetpgrp(int fd, int pgrp) {
    return sys_call2(S_TCSETPGRP, fd, pgrp);
}

long mount(void) {
    return sys_call0(S_FS_MOUNT);
}

long unmount(void) {
    return sys_call0(S_FS_UNMOUNT);
}

long pipe(int pipefd[2]) {
    return sys_call1(S_PIPE, (long)(uintptr_t)pipefd);
}

long ps(void) {
    return sys_call0(S_PS);
}

long exec(const char *path, char *const argv[]) {
    return sys_call2(S_EXEC, (long)(uintptr_t)path,
                     (long)(uintptr_t)argv);
}

char *getcwd(char *path, uint64_t size) {
    long err = sys_call2(S_GETCWD, (long)(uintptr_t)path, (long)size);
    return err < 0 ? (char *)(uintptr_t)err : path;
}

long stat(const char *path, void *st) {
    return sys_call2(S_STAT, (long)(uintptr_t)path, (long)(uintptr_t)st);
}

long tty_get_mode(int fd) {
    return sys_call1(S_TTY_GET_MODE, fd);
}

long tty_set_mode(int fd, int mode) {
    return sys_call2(S_TTY_SET_MODE, fd, mode);
}

long tty_get_size(int fd, int *rows, int *cols) {
    return sys_call3(S_TTY_GET_SIZE, fd, (long)(uintptr_t)rows,
                     (long)(uintptr_t)cols);
}

long tty_screen_enter(int fd) {
    return sys_call1(S_TTY_SCREEN_ENTER, fd);
}

long tty_screen_leave(int fd) {
    return sys_call1(S_TTY_SCREEN_LEAVE, fd);
}

long tty_screen_present(int fd, const char *cells, uint64_t count,
                        int cursor_row, int cursor_col) {
    return sys_call5(S_TTY_SCREEN_PRESENT, fd, (long)(uintptr_t)cells,
                     (long)count, cursor_row, cursor_col);
}

long proc_change_priority(int pid, int new_priority) {
    return sys_call2(S_PROC_CHANGE_PRIORITY, pid, new_priority);
}

long createlink(const char *create_path, const char *orig_path, int flags) {
    return sys_call3(S_CREATE_LINK, (long)(uintptr_t)create_path,
                     (long)(uintptr_t)orig_path, flags);
}

long readlink(const char *path, char *buffer, uint64_t count) {
    return sys_call3(S_READLINK, (long)(uintptr_t)path,
                     (long)(uintptr_t)buffer, (long)count);
}
