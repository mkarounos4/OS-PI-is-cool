#include "pipe.h"
#include "scheduler.h"
#include "oft.h"
#include "disk.h"
#include "inodes.h"
#include "ring_buffer.h"
#include "data-structs/ring_buffer.h"
#include "devices.h"
#include "data-structs/vec.h"
#include "scheduler/process.h"
#include "scheduler/scheduler.h"
#include "fs/oft.h"

#define PIPE_CAPACITY 4096

int pipe_open(struct oft_entry *entry);
int pipe_close(struct oft_entry *entry);
int pipe_read(struct oft_entry *entry, char *buffer, size_t count);
int pipe_write(struct oft_entry *entry, const char *buffer, size_t count);

static struct file_operations pipe_ops = {
    .open = pipe_open,
    .close = pipe_close,
    .read = pipe_read,
    .write = pipe_write,
};

static int install_process_fd(pcb_t *pcb, int k_fd) {
    for (size_t i = 0; i < vec_len(&pcb->file_descriptors); i++) {
        if (vec_get(&pcb->file_descriptors, i) == (void *)(uintptr_t)-1) {
            vec_set(&pcb->file_descriptors, i, (void *)(uintptr_t)k_fd);
            return (int)i;
        }
    }

    vec_push_back(&pcb->file_descriptors, (void *)(uintptr_t)k_fd);
    return (int)vec_len(&pcb->file_descriptors) - 1;
}

struct pipe_st {
    int num_readers;
    int num_writers;

    struct RingBuffer buffer;

    Vec rx_wait_queue;
    Vec tx_wait_queue;
};

static void pipe_free(struct pipe_st *pipe) {
    if (pipe == NULL) {
        return;
    }

    vec_destroy(&pipe->rx_wait_queue);
    vec_destroy(&pipe->tx_wait_queue);
    destroy_ring_buffer(&pipe->buffer);
    kfree(pipe);
}

int pipe(int pipefd[2]) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return -1;
    }

    struct pipe_st *pipe = kmalloc(sizeof(struct pipe_st));
    if (pipe == NULL) {
        return -1;
    }
    
    pipe->num_readers = 1;
    pipe->num_writers = 1;

    pipe->buffer = create_ring_buffer(PIPE_CAPACITY);

    pipe->rx_wait_queue = vec_new(2, NULL);
    pipe->tx_wait_queue = vec_new(2, NULL);


    ino_id_t ino_id;
    err_t err = add_new_file_inode(&ino_id, PIPE_TYPE, 0x7, &pipe_ops);
    if (err) {
        pipe_free(pipe);
        return err;
    }

    int read_fd = oft_open_file(O_RDONLY, NULL, ino_id, 0);
    if (read_fd < 0) {
        pipe_free(pipe);
        return read_fd;
    }

    int write_fd = oft_open_file(O_WRONLY, NULL, ino_id, 0);
    if (write_fd < 0) {
        pipe_free(pipe);
        return write_fd;
    }

    pipefd[0] = install_process_fd(pcb, read_fd);
    pipefd[1] = install_process_fd(pcb, write_fd);

    attributes_t metadata;
    err = get_inode_metadata(ino_id, &metadata);
    if (err) {
        pipe_free(pipe);
        return err;
    }

    metadata.i_pipe = pipe;

    err = set_inode_metadata(ino_id, &metadata);
    if (err) {
        pipe_free(pipe);
        return err;
    }

    return 0;
}

void pipe_wake_up_readers(struct pipe_st *pipe) {
    while (!vec_is_empty(&pipe->rx_wait_queue)) {
        void *pid;
        int removed = (int)(uintptr_t)vec_pop_back(&pipe->rx_wait_queue, &pid);
        if (!removed) {
            continue;
        }
        unblock_process(get_pcb_by_pid((int)(uintptr_t)pid));
    }
}

void pipe_wake_up_writers(struct pipe_st *pipe) {
    while (!vec_is_empty(&pipe->tx_wait_queue)) {
        void *pid;
        int removed = (int)(uintptr_t)vec_pop_back(&pipe->tx_wait_queue, &pid);
        if (!removed) {
            continue;
        }
        unblock_process(get_pcb_by_pid((int)(uintptr_t)pid));
    }
}

int pipe_open(struct oft_entry *entry) {
    struct pipe_st *pipe = entry->inode->inode.metadata.i_pipe;
    if (pipe == NULL) {
        return -1;
    }

    if (entry->mode & O_RDONLY) {
        pipe->num_readers++;
    }
    if (entry->mode & O_WRONLY) {
        pipe->num_writers++;
    }
    return 0;
}

int pipe_close(struct oft_entry *entry) {
    struct pipe_st *pipe = entry->inode->inode.metadata.i_pipe;
    if (pipe == NULL) {
        return -1;
    }

    if (entry->mode & O_RDONLY) {
        pipe->num_readers--;
    }
    if (entry->mode & O_WRONLY) {
        pipe->num_writers--;
        if (pipe->num_writers == 0) {
            char next_char = 0x04;
            int wrote = produce_ring_buffer(&pipe->buffer, &next_char);
            if (!wrote) {
                return -1;
            }
            pipe_wake_up_readers(pipe);
        }
    }

    if (pipe->num_readers == 0 && pipe->num_writers == 0) {
        // clean pipe
        pipe_free(pipe);
    }

    return 0;
}

int pipe_read(struct oft_entry *entry, char *buffer, size_t count) {
    struct pipe_st *pipe = entry->inode->inode.metadata.i_pipe;

    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return -1;
    }

    ssize_t num_read = 0;
    while (num_read < count) {
        char char_void;
        bool read_char = consume_ring_buffer(&pipe->buffer, &char_void);
        if (!read_char) {
            if (pipe->num_writers == 0) {
                return num_read;
            }
            pipe_wake_up_writers(pipe);
            vec_push_back(&pipe->rx_wait_queue, (ptr_t)(uintptr_t)curr_pcb->pid);
            block_process(curr_pcb);
            continue;
        }

        if (char_void == 0x04) {
            return num_read;
        }
        
        *buffer = char_void;
        buffer++;
        num_read++;
    }
    
    pipe_wake_up_writers(pipe);
    return num_read;
}

int pipe_write(struct oft_entry *entry, const char *buffer, size_t count) {
    struct pipe_st *pipe = entry->inode->inode.metadata.i_pipe;

    pcb_t *curr_pcb = get_curr_process();
    if (curr_pcb == NULL) {
        return -1;
    }

    ssize_t num_read = 0;
    while (num_read < count) {
        bool wrote_char = produce_ring_buffer(&pipe->buffer, buffer);
        if (!wrote_char) {
            pipe_wake_up_readers(pipe);
            vec_push_back(&pipe->tx_wait_queue, (ptr_t)(uintptr_t)curr_pcb->pid);
            block_process(curr_pcb);
            continue;
        }

        buffer++;
        num_read++;
    }
    
    pipe_wake_up_readers(pipe);
    return num_read;
}
