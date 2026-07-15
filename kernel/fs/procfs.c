#include "procfs.h"

#include <stdarg.h>

#include "devices.h"
#include "fs/caches/lru_cache.h"
#include "dirs.h"
#include "disk.h"
#include "fs/caches/inode_cache.h"
#include "inodes.h"
#include "irq.h"
#include "memory/page_table/page_table.h"
#include "scheduler/process.h"
#include "string.h"
#include "syscall/syscall.h"
#include "timer/timer.h"
#include "traps/traps.h"
#include "tty.h"
#include "uart/uart.h"
#include "virtual_fs.h"

#define PROC_KIND_ROOT_DIR 1
#define PROC_KIND_PID_DIR 2
#define PROC_KIND_FILE 3

#define PROC_FILE_PROCESSES 1
#define PROC_FILE_MEMINFO 2
#define PROC_FILE_UPTIME 3
#define PROC_FILE_VMSTAT 4
#define PROC_FILE_TIMERS 5
#define PROC_FILE_INTERRUPTS 6
#define PROC_FILE_SYSCALLS 7
#define PROC_FILE_CACHE 8
#define PROC_FILE_TTY 9
#define PROC_FILE_VERSION 10
#define PROC_FILE_CPUINFO 11
#define PROC_FILE_THREADS 12
#define PROC_FILE_LOCKS 13
#define PROC_FILE_PID_STATUS 14
#define PROC_FILE_PID_CWD 15
#define PROC_FILE_PID_FD 16
#define PROC_FILE_PID_MAPS 17
#define PROC_FILE_PID_THREADS 18

#define PROC_READ_BUFFER_SIZE 4096
#define PROC_INO_ROOT_DIR PROCFS_INO_BASE
#define PROC_INO_ROOT_FILE_BASE (PROCFS_INO_BASE + UINT32_C(0x1000))
#define PROC_INO_PID_DIR_BASE (PROCFS_INO_BASE + UINT32_C(0x2000))
#define PROC_INO_PID_FILE_BASE (PROCFS_INO_BASE + UINT32_C(0x3000))

struct proc_file_def {
    const char *name;
    uint8_t file_id;
};

static const struct proc_file_def proc_root_files[] = {
    {"processes", PROC_FILE_PROCESSES},
    {"meminfo", PROC_FILE_MEMINFO},
    {"uptime", PROC_FILE_UPTIME},
    {"vmstat", PROC_FILE_VMSTAT},
    {"timers", PROC_FILE_TIMERS},
    {"interrupts", PROC_FILE_INTERRUPTS},
    {"syscalls", PROC_FILE_SYSCALLS},
    {"cache", PROC_FILE_CACHE},
    {"tty", PROC_FILE_TTY},
    {"version", PROC_FILE_VERSION},
    {"cpuinfo", PROC_FILE_CPUINFO},
    {"threads", PROC_FILE_THREADS},
    {"locks", PROC_FILE_LOCKS},
};

static const struct proc_file_def proc_pid_files[] = {
    {"status", PROC_FILE_PID_STATUS},
    {"cwd", PROC_FILE_PID_CWD},
    {"fd", PROC_FILE_PID_FD},
    {"maps", PROC_FILE_PID_MAPS},
    {"threads", PROC_FILE_PID_THREADS},
};

static ino_id_t proc_root_ino = PROC_INO_ROOT_DIR;
static struct proc_st *proc_root_node;
static struct proc_st *proc_root_file_nodes[sizeof(proc_root_files) /
                                            sizeof(proc_root_files[0])];
static struct proc_st *proc_pid_dir_nodes[MAX_PROCESS_COUNT];
static struct proc_st *proc_pid_file_nodes[MAX_PROCESS_COUNT]
                                      [sizeof(proc_pid_files) /
                                       sizeof(proc_pid_files[0])];

static int proc_dir_open(struct oft_entry *entry);
static int proc_dir_close(struct oft_entry *entry);
static int proc_emit_dirent(struct fs_dirent *out, const char *name,
                            ino_id_t ino);
static int proc_dir_lookup(const char *f_name, uint8_t is_dir_type,
                           struct fs_dirent *dirent, int curr_dir);
static int proc_dir_readdir(struct oft_entry *dir, struct fs_dirent *out);
static int proc_file_open(struct oft_entry *entry);
static int proc_file_close(struct oft_entry *entry);
static int proc_file_read(struct oft_entry *entry, char *buffer, size_t count);
static err_t procfs_format_path(ino_id_t ino, char *path, size_t size);

static struct file_operations proc_dir_ops = {
    .open = proc_dir_open,
    .close = proc_dir_close,
    .read = NULL,
    .write = NULL,
    .lookup = proc_dir_lookup,
    .readdir = proc_dir_readdir,
    .getattr = NULL,
};

static struct file_operations proc_file_ops = {
    .open = proc_file_open,
    .close = proc_file_close,
    .read = proc_file_read,
    .write = NULL,
    .lookup = NULL,
    .readdir = NULL,
    .getattr = NULL,
};

static const struct virtual_fs_ops procfs_vfs_ops = {
    .is_inode = procfs_is_virtual_inode,
    .get_metadata = procfs_get_metadata,
    .alloc_cached_inode = procfs_alloc_cached_inode,
    .free_cached_inode = procfs_free_cached_inode,
    .format_path = procfs_format_path,
};

static int proc_file_count(const struct proc_file_def *files, size_t bytes) {
    (void)files;
    return (int)(bytes / sizeof(struct proc_file_def));
}

static int proc_root_file_count(void) {
    return proc_file_count(proc_root_files, sizeof(proc_root_files));
}

static int proc_pid_file_count(void) {
    return proc_file_count(proc_pid_files, sizeof(proc_pid_files));
}

static ino_id_t proc_root_file_ino(int index) {
    return PROC_INO_ROOT_FILE_BASE + (ino_id_t)index;
}

static ino_id_t proc_pid_dir_ino(pid_t pid) {
    return PROC_INO_PID_DIR_BASE + (ino_id_t)pid;
}

static ino_id_t proc_pid_file_ino(pid_t pid, int index) {
    return PROC_INO_PID_FILE_BASE +
           (ino_id_t)pid * (ino_id_t)proc_pid_file_count() +
           (ino_id_t)index;
}

static int parse_pid(const char *name, pid_t *pid) {
    if (name == NULL || name[0] == '\0' || pid == NULL) {
        return INVALID_ARGS;
    }

    int value = 0;
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return INVALID_ARGS;
        }
        value = value * 10 + (name[i] - '0');
        if (value >= MAX_PROCESS_COUNT) {
            return FILE_NOT_FOUND;
        }
    }

    *pid = value;
    return SUCCESS;
}

static void write_pid_name(char name[32], pid_t pid) {
    snprintf(name, 32, "%d", pid);
}

static char process_state_char(enum process_state state) {
    switch (state) {
    case PROC_RUNNING_STATE:
        return 'R';
    case PROC_BLOCKED_STATE:
        return 'B';
    case PROC_STOPPED_STATE:
        return 'T';
    case PROC_ZOMBIE_STATE:
        return 'Z';
    case PROC_UNUSED_STATE:
    default:
        return 'U';
    }
}

static const char *process_state_name(enum process_state state) {
    switch (state) {
    case PROC_RUNNING_STATE:
        return "running";
    case PROC_BLOCKED_STATE:
        return "blocked";
    case PROC_STOPPED_STATE:
        return "stopped";
    case PROC_ZOMBIE_STATE:
        return "zombie";
    case PROC_UNUSED_STATE:
    default:
        return "unused";
    }
}

static const char *proc_thread_state_name(enum thread_state state) {
    switch (state) {
    case THREAD_READY:
        return "runnable";
    case THREAD_RUNNING:
        return "running";
    case THREAD_STOPPED:
        return "sleep";
    case THREAD_ZOMBIE:
        return "zombie";
    case THREAD_BLOCKED_INTERRUPTABLE:
    case THREAD_BLOCKED_UNINTERUPTABLE:
    case THREAD_BLOCKED_KILLABLE:
        return "blocked";
    case THREAD_UNUSED:
    default:
        return "unused";
    }
}

static const char *proc_root_thread_state_name(enum thread_state state) {
    switch (state) {
    case THREAD_READY:
        return "runnable";
    case THREAD_RUNNING:
        return "running";
    case THREAD_STOPPED:
    case THREAD_BLOCKED_INTERRUPTABLE:
    case THREAD_BLOCKED_UNINTERUPTABLE:
    case THREAD_BLOCKED_KILLABLE:
        return "sleep";
    case THREAD_ZOMBIE:
        return "zombie";
    case THREAD_UNUSED:
    default:
        return "unused";
    }
}

static const char *proc_thread_name(pcb_t *pcb, tcb_t *thread,
                                    char *buf, size_t size) {
    if (pcb == NULL || thread == NULL || buf == NULL || size == 0) {
        return "?";
    }

    const char *role = "thread";
    if (vec_len(&pcb->tids) > 0 &&
        (tid_t)(uintptr_t)vec_get(&pcb->tids, 0) == thread->tid) {
        role = "main";
    }

    snprintf(buf, size, "%s.%s", pcb->name, role);
    return buf;
}

static struct proc_st *proc_alloc_node(uint8_t kind, pid_t pid,
                                       uint8_t file_id) {
    struct proc_st *proc = kmalloc(sizeof(struct proc_st));
    if (proc == NULL) {
        return NULL;
    }

    proc->kind = kind;
    proc->file_id = file_id;
    proc->pid = pid;
    proc->offset = 0;
    proc->buffer = NULL;
    proc->size = 0;
    return proc;
}

static void proc_free_open_buffer(struct proc_st *proc) {
    if (proc == NULL) {
        return;
    }

    if (proc->buffer != NULL) {
        kfree(proc->buffer);
        proc->buffer = NULL;
    }
    proc->size = 0;
}

static err_t proc_prepare_node(struct proc_st **slot, uint8_t kind,
                               pid_t pid, uint8_t file_id,
                               struct proc_st **proc_out) {
    if (slot == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = *slot;
    if (proc == NULL) {
        proc = proc_alloc_node(kind, pid, file_id);
        if (proc == NULL) {
            return NO_FREE_BLOCKS;
        }
        *slot = proc;
    } else {
        proc->kind = kind;
        proc->pid = pid;
        proc->file_id = file_id;
    }

    if (proc_out != NULL) {
        *proc_out = proc;
    }
    return SUCCESS;
}

static void proc_fill_metadata(attributes_t *metadata, struct proc_st *proc,
                               struct file_operations *fops,
                               uint8_t type, uint8_t perm) {
    memset(metadata, 0, sizeof(*metadata));
    metadata->i_links_count = 1;
    metadata->type = type;
    metadata->perm = perm;
    metadata->mtime = timer_get_ticks();
    metadata->fops = fops;
    metadata->i_proc = proc;
}

err_t procfs_init(void) {
    for (int i = 0; i < proc_root_file_count(); i++) {
        proc_root_file_nodes[i] = NULL;
    }
    proc_root_node = NULL;
    for (int pid = 0; pid < MAX_PROCESS_COUNT; pid++) {
        proc_pid_dir_nodes[pid] = NULL;
        for (int i = 0; i < proc_pid_file_count(); i++) {
            proc_pid_file_nodes[pid][i] = NULL;
        }
    }

    err_t err = proc_prepare_node(&proc_root_node, PROC_KIND_ROOT_DIR,
                                  -1, 0, NULL);
    if (err != SUCCESS) {
        return err;
    }

    return vfs_register_root_mount("proc", proc_root_ino, &procfs_vfs_ops);
}

static err_t proc_ensure_root_file(int index, ino_id_t *ino) {
    if (index < 0 || index >= proc_root_file_count() || ino == NULL) {
        return INVALID_ARGS;
    }

    err_t err = proc_prepare_node(&proc_root_file_nodes[index],
                                  PROC_KIND_FILE, -1,
                                  proc_root_files[index].file_id, NULL);
    if (err != SUCCESS) {
        return err;
    }

    *ino = proc_root_file_ino(index);
    return SUCCESS;
}

static err_t proc_ensure_pid_dir(pid_t pid, ino_id_t *ino) {
    if (pid < 0 || pid >= MAX_PROCESS_COUNT || ino == NULL) {
        return INVALID_ARGS;
    }
    if (get_pcb_by_pid(pid) == NULL) {
        return FILE_NOT_FOUND;
    }

    err_t err = proc_prepare_node(&proc_pid_dir_nodes[pid],
                                  PROC_KIND_PID_DIR, pid, 0, NULL);
    if (err != SUCCESS) {
        return err;
    }

    *ino = proc_pid_dir_ino(pid);
    return SUCCESS;
}

static err_t proc_ensure_pid_file(pid_t pid, int index, ino_id_t *ino) {
    if (pid < 0 || pid >= MAX_PROCESS_COUNT || index < 0 ||
        index >= proc_pid_file_count() || ino == NULL) {
        return INVALID_ARGS;
    }
    if (get_pcb_by_pid(pid) == NULL) {
        return FILE_NOT_FOUND;
    }

    err_t err = proc_prepare_node(&proc_pid_file_nodes[pid][index],
                                  PROC_KIND_FILE, pid,
                                  proc_pid_files[index].file_id, NULL);
    if (err != SUCCESS) {
        return err;
    }

    *ino = proc_pid_file_ino(pid, index);
    return SUCCESS;
}

int procfs_is_virtual_inode(ino_id_t ino) {
    if (ino == PROC_INO_ROOT_DIR) {
        return 1;
    }
    if (ino >= PROC_INO_ROOT_FILE_BASE &&
        ino < PROC_INO_ROOT_FILE_BASE + (ino_id_t)proc_root_file_count()) {
        return 1;
    }
    if (ino >= PROC_INO_PID_DIR_BASE &&
        ino < PROC_INO_PID_DIR_BASE + (ino_id_t)MAX_PROCESS_COUNT) {
        return 1;
    }

    ino_id_t pid_file_count =
        (ino_id_t)MAX_PROCESS_COUNT * (ino_id_t)proc_pid_file_count();
    return ino >= PROC_INO_PID_FILE_BASE &&
           ino < PROC_INO_PID_FILE_BASE + pid_file_count;
}

static err_t procfs_format_path(ino_id_t ino, char *path, size_t size) {
    if (path == NULL || size == 0 || !procfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    if (ino == PROC_INO_ROOT_DIR) {
        if (size < 6) {
            return INVALID_ARGS;
        }
        strcpy(path, "/proc");
        return SUCCESS;
    }

    if (ino >= PROC_INO_PID_DIR_BASE &&
        ino < PROC_INO_PID_DIR_BASE + (ino_id_t)MAX_PROCESS_COUNT) {
        pid_t pid = (pid_t)(ino - PROC_INO_PID_DIR_BASE);
        if (get_pcb_by_pid(pid) == NULL) {
            return FILE_NOT_FOUND;
        }
        if (snprintf(path, size, "/proc/%d", pid) >= (int)size) {
            return INVALID_ARGS;
        }
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

err_t procfs_get_metadata(ino_id_t ino, attributes_t *metadata) {
    if (metadata == NULL || !procfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    if (ino == PROC_INO_ROOT_DIR) {
        struct proc_st *proc;
        err_t err = proc_prepare_node(&proc_root_node, PROC_KIND_ROOT_DIR,
                                      -1, 0, &proc);
        if (err != SUCCESS) {
            return err;
        }

        proc_fill_metadata(metadata, proc, &proc_dir_ops, DIRECTORY_TYPE, 0x5);
        return SUCCESS;
    }

    if (ino >= PROC_INO_ROOT_FILE_BASE &&
        ino < PROC_INO_ROOT_FILE_BASE + (ino_id_t)proc_root_file_count()) {
        int index = (int)(ino - PROC_INO_ROOT_FILE_BASE);
        struct proc_st *proc;
        err_t err = proc_prepare_node(&proc_root_file_nodes[index],
                                      PROC_KIND_FILE, -1,
                                      proc_root_files[index].file_id, &proc);
        if (err != SUCCESS) {
            return err;
        }

        proc_fill_metadata(metadata, proc, &proc_file_ops, PROC_TYPE, 0x4);
        return SUCCESS;
    }

    if (ino >= PROC_INO_PID_DIR_BASE &&
        ino < PROC_INO_PID_DIR_BASE + (ino_id_t)MAX_PROCESS_COUNT) {
        pid_t pid = (pid_t)(ino - PROC_INO_PID_DIR_BASE);
        if (get_pcb_by_pid(pid) == NULL) {
            return FILE_NOT_FOUND;
        }

        struct proc_st *proc;
        err_t err = proc_prepare_node(&proc_pid_dir_nodes[pid],
                                      PROC_KIND_PID_DIR, pid, 0, &proc);
        if (err != SUCCESS) {
            return err;
        }

        proc_fill_metadata(metadata, proc, &proc_dir_ops, DIRECTORY_TYPE, 0x5);
        return SUCCESS;
    }

    ino_id_t pid_file_start = PROC_INO_PID_FILE_BASE;
    ino_id_t pid_file_count =
        (ino_id_t)MAX_PROCESS_COUNT * (ino_id_t)proc_pid_file_count();
    if (ino >= pid_file_start && ino < pid_file_start + pid_file_count) {
        ino_id_t raw = ino - pid_file_start;
        pid_t pid = (pid_t)(raw / (ino_id_t)proc_pid_file_count());
        int index = (int)(raw % (ino_id_t)proc_pid_file_count());
        if (get_pcb_by_pid(pid) == NULL) {
            return FILE_NOT_FOUND;
        }

        struct proc_st *proc;
        err_t err = proc_prepare_node(&proc_pid_file_nodes[pid][index],
                                      PROC_KIND_FILE, pid,
                                      proc_pid_files[index].file_id, &proc);
        if (err != SUCCESS) {
            return err;
        }

        proc_fill_metadata(metadata, proc, &proc_file_ops, PROC_TYPE, 0x4);
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

err_t procfs_alloc_cached_inode(ino_id_t ino, struct cached_inode_st **node) {
    if (node == NULL || !procfs_is_virtual_inode(ino)) {
        return INVALID_ARGS;
    }

    struct cached_inode_st *cached = kmalloc(sizeof(*cached));
    if (cached == NULL) {
        return NO_FREE_BLOCKS;
    }
    memset(cached, 0, sizeof(*cached));

    err_t err = procfs_get_metadata(ino, &cached->inode.metadata);
    if (err != SUCCESS) {
        kfree(cached);
        return err;
    }

    cached->id = ino;
    cached->dirty = 0;
    *node = cached;
    return SUCCESS;
}

void procfs_free_cached_inode(struct cached_inode_st *node) {
    if (node == NULL) {
        return;
    }
    kfree(node);
}

static int proc_dir_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = entry->inode->inode.metadata.i_proc;
    if (proc == NULL ||
        (proc->kind != PROC_KIND_ROOT_DIR &&
         proc->kind != PROC_KIND_PID_DIR)) {
        return INVALID_ARGS;
    }

    proc->offset = 0;
    return SUCCESS;
}

static int proc_dir_close(struct oft_entry *entry) {
    (void)entry;
    return SUCCESS;
}

static int proc_lookup_root(const char *f_name, uint8_t is_dir_type,
                            struct fs_dirent *dirent) {
    if (is_dir_type) {
        pid_t pid;
        err_t err = parse_pid(f_name, &pid);
        if (err != SUCCESS) {
            return err;
        }

        ino_id_t ino;
        err = proc_ensure_pid_dir(pid, &ino);
        if (err != SUCCESS) {
            return err;
        }

        if (dirent != NULL) {
            memset(dirent, 0, sizeof(*dirent));
            write_pid_name(dirent->name, pid);
            dirent->ino_id = ino;
        }
        return SUCCESS;
    }

    for (int i = 0; i < proc_root_file_count(); i++) {
        if (strcmp(f_name, proc_root_files[i].name) != 0) {
            continue;
        }

        ino_id_t ino;
        err_t err = proc_ensure_root_file(i, &ino);
        if (err != SUCCESS) {
            return err;
        }

        if (dirent != NULL) {
            memset(dirent, 0, sizeof(*dirent));
            strcpy(dirent->name, proc_root_files[i].name);
            dirent->ino_id = ino;
        }
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

static int proc_lookup_pid_dir(struct proc_st *proc, const char *f_name,
                               uint8_t is_dir_type,
                               struct fs_dirent *dirent) {
    if (proc == NULL || is_dir_type) {
        return FILE_NOT_FOUND;
    }

    for (int i = 0; i < proc_pid_file_count(); i++) {
        if (strcmp(f_name, proc_pid_files[i].name) != 0) {
            continue;
        }

        ino_id_t ino;
        err_t err = proc_ensure_pid_file(proc->pid, i, &ino);
        if (err != SUCCESS) {
            return err;
        }

        if (dirent != NULL) {
            memset(dirent, 0, sizeof(*dirent));
            strcpy(dirent->name, proc_pid_files[i].name);
            dirent->ino_id = ino;
        }
        return SUCCESS;
    }

    return FILE_NOT_FOUND;
}

static int proc_dir_lookup(const char *f_name, uint8_t is_dir_type,
                           struct fs_dirent *dirent, int curr_dir) {
    attributes_t metadata;
    err_t err = get_inode_metadata(curr_dir, &metadata);
    if (err != SUCCESS) {
        return err;
    }

    struct proc_st *proc = metadata.i_proc;
    if (proc == NULL) {
        return INVALID_ARGS;
    }

    if (is_dir_type && strcmp(f_name, ".") == 0) {
        return proc_emit_dirent(dirent, ".", (ino_id_t)curr_dir);
    }
    if (is_dir_type && strcmp(f_name, "..") == 0) {
        if (proc->kind == PROC_KIND_ROOT_DIR) {
            return proc_emit_dirent(dirent, "..", ROOT_INO);
        }
        if (proc->kind == PROC_KIND_PID_DIR) {
            return proc_emit_dirent(dirent, "..", proc_root_ino);
        }
    }

    if (proc->kind == PROC_KIND_ROOT_DIR) {
        return proc_lookup_root(f_name, is_dir_type, dirent);
    }
    if (proc->kind == PROC_KIND_PID_DIR) {
        return proc_lookup_pid_dir(proc, f_name, is_dir_type, dirent);
    }

    return INVALID_ARGS;
}

static int proc_emit_dirent(struct fs_dirent *out, const char *name,
                            ino_id_t ino) {
    memset(out, 0, sizeof(*out));
    strcpy(out->name, name);
    out->ino_id = ino;
    return SUCCESS;
}

static int proc_root_readdir(struct proc_st *proc, struct fs_dirent *out) {
    if (proc->offset == 0) {
        proc->offset++;
        return proc_emit_dirent(out, ".", proc_root_ino);
    }
    if (proc->offset == 1) {
        proc->offset++;
        return proc_emit_dirent(out, "..", ROOT_INO);
    }

    uint32_t file_index = proc->offset - 2;
    if (file_index < (uint32_t)proc_root_file_count()) {
        ino_id_t ino;
        err_t err = proc_ensure_root_file((int)file_index, &ino);
        if (err != SUCCESS) {
            return err;
        }

        proc->offset++;
        return proc_emit_dirent(out, proc_root_files[file_index].name, ino);
    }

    for (pid_t pid = (pid_t)(proc->offset - 2 -
                             (uint32_t)proc_root_file_count());
         pid < MAX_PROCESS_COUNT; pid++) {
        if (get_pcb_by_pid(pid) == NULL) {
            continue;
        }

        ino_id_t ino;
        err_t err = proc_ensure_pid_dir(pid, &ino);
        if (err != SUCCESS) {
            return err;
        }

        char name[32];
        write_pid_name(name, pid);
        proc->offset = (uint32_t)pid + 3 +
                       (uint32_t)proc_root_file_count();
        return proc_emit_dirent(out, name, ino);
    }

    return FILE_NOT_FOUND;
}

static int proc_pid_readdir(struct proc_st *proc, struct fs_dirent *out) {
    if (proc->offset == 0) {
        proc->offset++;
        return proc_emit_dirent(out, ".", proc_pid_dir_ino(proc->pid));
    }
    if (proc->offset == 1) {
        proc->offset++;
        return proc_emit_dirent(out, "..", proc_root_ino);
    }

    uint32_t index = proc->offset - 2;
    if (index >= (uint32_t)proc_pid_file_count()) {
        return FILE_NOT_FOUND;
    }

    ino_id_t ino;
    err_t err = proc_ensure_pid_file(proc->pid, (int)index, &ino);
    if (err != SUCCESS) {
        return err;
    }

    proc->offset++;
    return proc_emit_dirent(out, proc_pid_files[index].name, ino);
}

static int proc_dir_readdir(struct oft_entry *dir, struct fs_dirent *out) {
    if (dir == NULL || dir->inode == NULL || out == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = dir->inode->inode.metadata.i_proc;
    if (proc == NULL) {
        return INVALID_ARGS;
    }
    if (proc->kind == PROC_KIND_ROOT_DIR) {
        return proc_root_readdir(proc, out);
    }
    if (proc->kind == PROC_KIND_PID_DIR) {
        return proc_pid_readdir(proc, out);
    }

    return INVALID_ARGS;
}

static int append(char *buf, size_t size, int len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t used = len < (int)size ? (size_t)len : size - 1;
    int ret = vsnprintf(buf + used, size - used, fmt, args);
    va_end(args);
    if (ret < 0) {
        return ret;
    }
    return len + ret;
}

static int build_processes(char *buf, size_t size) {
    int len = snprintf(buf, size, "PID PPID PGID STATE THREADS NAME\n");
    for (pid_t pid = 0; pid < MAX_PROCESS_COUNT; pid++) {
        pcb_t *pcb = get_pcb_by_pid(pid);
        if (pcb == NULL) {
            continue;
        }

        len = append(buf, size, len, "%d %d %d %c %u %s\n",
                     pcb->pid, pcb->ppid, pcb->pgid,
                     process_state_char(pcb->state),
                     (unsigned int)vec_len(&pcb->tids), pcb->name);
    }
    return len;
}

static unsigned int proc_count_open_files(pcb_t *pcb) {
    unsigned int count = 0;
    for (size_t i = 0; i < vec_len(&pcb->file_descriptors); i++) {
        int fd = (int)(uintptr_t)vec_get(&pcb->file_descriptors, i);
        if (fd >= 0) {
            count++;
        }
    }
    return count;
}

struct proc_thread_metrics {
    unsigned int total;
    unsigned int running;
    unsigned int blocked;
    unsigned int stopped;
    unsigned int zombie;
    int min_priority;
    uint32_t blocked_until;
};

static void proc_collect_thread_metrics(pcb_t *pcb,
                                        struct proc_thread_metrics *metrics) {
    memset(metrics, 0, sizeof(*metrics));
    metrics->min_priority = 0;

    for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
        tid_t tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
        tcb_t *thread = thread_get_by_tid(tid);
        if (thread == NULL) {
            continue;
        }

        metrics->total++;
        if (metrics->total == 1 || thread->priority < metrics->min_priority) {
            metrics->min_priority = thread->priority;
        }
        metrics->blocked_until |= thread->blocked_until;

        switch (thread->state) {
        case THREAD_READY:
        case THREAD_RUNNING:
            metrics->running++;
            break;
        case THREAD_STOPPED:
            metrics->stopped++;
            break;
        case THREAD_ZOMBIE:
            metrics->zombie++;
            break;
        case THREAD_BLOCKED_INTERRUPTABLE:
        case THREAD_BLOCKED_UNINTERUPTABLE:
        case THREAD_BLOCKED_KILLABLE:
            metrics->blocked++;
            break;
        case THREAD_UNUSED:
        default:
            break;
        }
    }
}

static int build_pid_status(pcb_t *pcb, char *buf, size_t size) {
    struct proc_thread_metrics metrics;
    proc_collect_thread_metrics(pcb, &metrics);

    return snprintf(buf, size,
                    "Name: %s\n"
                    "Pid: %d\n"
                    "PPid: %d\n"
                    "Pgid: %d\n"
                    "State: %s\n"
                    "Threads: %u\n"
                    "ThreadsRunning: %u\n"
                    "ThreadsBlocked: %u\n"
                    "ThreadsStopped: %u\n"
                    "ThreadsZombie: %u\n"
                    "MinPriority: %d\n"
                    "ExitCode: %d\n"
                    "CwdIno: %u\n"
                    "OpenFiles: %u\n"
                    "SignalsPending: 0x%x\n"
                    "BlockedUntil: %u\n"
                    "TTBR0: 0x%lx\n",
                    pcb->name,
                    pcb->pid,
                    pcb->ppid,
                    pcb->pgid,
                    process_state_name(pcb->state),
                    metrics.total,
                    metrics.running,
                    metrics.blocked,
                    metrics.stopped,
                    metrics.zombie,
                    metrics.min_priority,
                    pcb->exit_code,
                    pcb->cwd,
                    proc_count_open_files(pcb),
                    (unsigned int)pcb->pending_signals,
                    metrics.blocked_until,
                    (unsigned long)pcb->ttbr0_el1);
}

static int build_pid_fd(pcb_t *pcb, char *buf, size_t size) {
    int len = snprintf(buf, size, "FD KERNEL_FD\n");
    for (size_t i = 0; i < vec_len(&pcb->file_descriptors); i++) {
        int k_fd = (int)(uintptr_t)vec_get(&pcb->file_descriptors, i);
        if (k_fd < 0) {
            continue;
        }
        len = append(buf, size, len, "%u %d\n", (unsigned int)i, k_fd);
    }
    return len;
}

static int build_pid_threads(pcb_t *pcb, char *buf, size_t size) {
    int len = snprintf(buf, size, "TID STATE KSTACK USTACK NAME\n");
    for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
        tid_t tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
        tcb_t *thread = thread_get_by_tid(tid);
        if (thread == NULL) {
            continue;
        }

        char name[48];
        len = append(buf, size, len, "%d %s 0x%lx 0x%lx %s\n",
                     thread->tid,
                     proc_thread_state_name(thread->state),
                     (unsigned long)(uintptr_t)thread->kernel_stack,
                     (unsigned long)(uintptr_t)thread->user_stack_va,
                     proc_thread_name(pcb, thread, name, sizeof(name)));
    }
    return len;
}

static int build_threads(char *buf, size_t size) {
    int len = snprintf(buf, size, "TID PID STATE CPU NAME\n");
    for (pid_t pid = 0; pid < MAX_PROCESS_COUNT; pid++) {
        pcb_t *pcb = get_pcb_by_pid(pid);
        if (pcb == NULL) {
            continue;
        }

        for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
            tid_t tid = (tid_t)(uintptr_t)vec_get(&pcb->tids, i);
            tcb_t *thread = thread_get_by_tid(tid);
            if (thread == NULL) {
                continue;
            }

            char name[48];
            len = append(buf, size, len, "%d %d %s 0 %s\n",
                         thread->tid,
                         pcb->pid,
                         proc_root_thread_state_name(thread->state),
                         proc_thread_name(pcb, thread, name, sizeof(name)));
        }
    }
    return len;
}

static int build_cache(char *buf, size_t size) {
    struct lru_cache_stats block_stats;
    struct inode_cache_stats inode_stats;
    lru_cache_get_stats(&block_stats);
    inode_cache_get_stats(&inode_stats);

    return snprintf(buf, size,
                    "BlockCache:\n"
                    "  capacity_blocks: %u\n"
                    "  used_blocks: %u\n"
                    "  hits: %u\n"
                    "  misses: %u\n"
                    "  evictions: %u\n"
                    "  dirty_blocks: %u\n"
                    "\n"
                    "InodeCache:\n"
                    "  capacity: %u\n"
                    "  used: %u\n"
                    "  hits: %u\n"
                    "  misses: %u\n"
                    "  evictions: %u\n"
                    "  dirty: %u\n",
                    block_stats.capacity_blocks,
                    block_stats.used_blocks,
                    block_stats.hits,
                    block_stats.misses,
                    block_stats.evictions,
                    block_stats.dirty_blocks,
                    inode_stats.capacity,
                    inode_stats.used,
                    inode_stats.hits,
                    inode_stats.misses,
                    inode_stats.evictions,
                    inode_stats.dirty);
}

static int build_proc_file(struct proc_st *proc, char *buf, size_t size) {
    if (proc->file_id >= PROC_FILE_PID_STATUS) {
        pcb_t *pcb = get_pcb_by_pid(proc->pid);
        if (pcb == NULL) {
            return FILE_NOT_FOUND;
        }

        switch (proc->file_id) {
        case PROC_FILE_PID_STATUS:
            return build_pid_status(pcb, buf, size);
        case PROC_FILE_PID_CWD:
            return snprintf(buf, size, "%u\n", pcb->cwd);
        case PROC_FILE_PID_FD:
            return build_pid_fd(pcb, buf, size);
        case PROC_FILE_PID_MAPS:
            return page_table_format_segments(
                (uint64_t *)(uintptr_t)pcb->ttbr0_el1_va, buf, size);
        case PROC_FILE_PID_THREADS:
            return build_pid_threads(pcb, buf, size);
        default:
            return FILE_NOT_FOUND;
        }
    }

    switch (proc->file_id) {
    case PROC_FILE_PROCESSES:
        return build_processes(buf, size);
    case PROC_FILE_MEMINFO:
        return page_table_format_meminfo(buf, size);
    case PROC_FILE_UPTIME:
        return snprintf(buf, size, "ticks: %u\nfrequency: %u\n",
                        (unsigned int)timer_get_ticks(),
                        (unsigned int)timer_get_frequency());
    case PROC_FILE_VMSTAT:
        return page_table_format_vmstat(buf, size);
    case PROC_FILE_TIMERS:
        return timer_format_proc(buf, size);
    case PROC_FILE_INTERRUPTS:
        return irq_format_proc(buf, size);
    case PROC_FILE_SYSCALLS:
        return syscall_format_proc(buf, size);
    case PROC_FILE_CACHE:
        return build_cache(buf, size);
    case PROC_FILE_TTY:
        return tty_format_proc(buf, size);
    case PROC_FILE_VERSION:
        return snprintf(buf, size,
                        "OS-PI-is-cool 0.1\n"
                        "arch: aarch64\n"
#ifdef PLATFORM_RPI5
                        "platform: rpi5\n"
#elif defined(PLATFORM_QEMU)
                        "platform: qemu-raspi3b\n"
#else
                        "platform: unknown\n"
#endif
                        "build: " __DATE__ " " __TIME__ "\n"
                        "compiler: aarch64-none-elf-gcc\n");
    case PROC_FILE_CPUINFO:
        return snprintf(buf, size,
#ifdef PLATFORM_RPI5
                        "processor: 0\n"
                        "arch: AArch64\n"
                        "exception_level: EL%u\n"
                        "platform: Raspberry Pi 5 / BCM2712\n"
                        "page_size: %u\n"
                        "timer: ARM generic timer\n",
                        (unsigned int)cpu_current_el(),
                        (unsigned int)PAGE_SIZE
#elif defined(PLATFORM_QEMU)
                        "processor: 0\n"
                        "arch: AArch64\n"
                        "exception_level: EL%u\n"
                        "platform: QEMU raspi3b\n"
                        "page_size: %u\n"
                        "timer: ARM generic timer\n",
                        (unsigned int)cpu_current_el(),
                        (unsigned int)PAGE_SIZE
#else
                        "processor: 0\n"
                        "arch: AArch64\n"
                        "exception_level: EL%u\n"
                        "platform: unknown\n"
                        "page_size: %u\n"
                        "timer: ARM generic timer\n",
                        (unsigned int)cpu_current_el(),
                        (unsigned int)PAGE_SIZE
#endif
        );
    case PROC_FILE_THREADS:
        return build_threads(buf, size);
    case PROC_FILE_LOCKS:
        return threading_format_locks(buf, size);
    default:
        return FILE_NOT_FOUND;
    }
}

static int proc_file_open(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = entry->inode->inode.metadata.i_proc;
    if (proc == NULL || proc->kind != PROC_KIND_FILE) {
        return INVALID_ARGS;
    }

    proc_free_open_buffer(proc);
    proc->buffer = kmalloc(PROC_READ_BUFFER_SIZE);
    if (proc->buffer == NULL) {
        return NO_FREE_BLOCKS;
    }

    int len = build_proc_file(proc, proc->buffer, PROC_READ_BUFFER_SIZE);
    if (len < 0) {
        proc_free_open_buffer(proc);
        return len;
    }
    if (len >= PROC_READ_BUFFER_SIZE) {
        len = PROC_READ_BUFFER_SIZE - 1;
    }

    proc->size = (uint32_t)len;
    entry->cursor = 0;
    entry->inode->inode.metadata.i_size = proc->size;
    entry->inode->dirty = 1;
    return SUCCESS;
}

static int proc_file_close(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = entry->inode->inode.metadata.i_proc;
    proc_free_open_buffer(proc);
    return SUCCESS;
}

static int proc_file_read(struct oft_entry *entry, char *buffer, size_t count) {
    if (entry == NULL || entry->inode == NULL || buffer == NULL) {
        return INVALID_ARGS;
    }

    struct proc_st *proc = entry->inode->inode.metadata.i_proc;
    if (proc == NULL || proc->kind != PROC_KIND_FILE ||
        proc->buffer == NULL) {
        return INVALID_ARGS;
    }

    if (entry->cursor >= proc->size) {
        return 0;
    }

    uint32_t remaining = proc->size - entry->cursor;
    uint32_t to_read = count < remaining ? (uint32_t)count : remaining;
    memcpy(buffer, proc->buffer + entry->cursor, to_read);
    entry->cursor += to_read;
    return (int)to_read;
}
