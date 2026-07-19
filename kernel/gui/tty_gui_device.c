#include "tty_gui_device.h"

#include "tty_gui.h"
#include "devices/devices.h"
#include "devices/tty.h"
#include "scheduler/scheduler.h"
#include "threading/thread.h"

#define TTY_GUI_DEVICE_BUFFER_SIZE 4096
#define TTY_GUI_DEVICE_COUNT MAX_TTY_DEVICES

struct tty_gui_device {
    struct RingBuffer rx;
    struct RingBuffer tx;
    Vec rx_wait_queue;
    int refcount;
    uint8_t active;
};

static struct tty_gui_device tty_gui_devices[TTY_GUI_DEVICE_COUNT];

static int tty_gui_dev_open(struct oft_entry *entry);
static int tty_gui_dev_close(struct oft_entry *entry);
static int tty_gui_dev_read(struct oft_entry *entry, char *buffer, size_t count);
static int tty_gui_dev_write(struct oft_entry *entry, const char *buffer, size_t count);

static const struct file_operations tty_gui_fops = {
    .open = tty_gui_dev_open,
    .close = tty_gui_dev_close,
    .read = tty_gui_dev_read,
    .write = tty_gui_dev_write,
};

static struct char_driver tty_gui_char_driver = {
    .name = "ttygui",
    .major = TTY_GUI_MAJOR,
    .fops = &tty_gui_fops,
    .driver_data = tty_gui_devices,
};

static struct tty_gui_device *tty_gui_from_entry(struct oft_entry *entry,
                                                 int *minor_out) {
    if (entry == NULL || entry->inode == NULL) {
        return NULL;
    }

    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= TTY_GUI_DEVICE_COUNT || !tty_gui_devices[minor].active) {
        return NULL;
    }

    if (minor_out != NULL) {
        *minor_out = minor;
    }
    return &tty_gui_devices[minor];
}

static int wait_queue_has_tid(Vec *queue, tid_t tid) {
    for (size_t i = 0; i < vec_len(queue); i++) {
        if ((tid_t)(uintptr_t)vec_get(queue, i) == tid) {
            return 1;
        }
    }
    return 0;
}

static void tty_gui_device_clear_buffers(struct tty_gui_device *dev) {
    dev->rx.head = NULL;
    dev->rx.tail = NULL;
    dev->rx.size = 0;
    dev->tx.head = NULL;
    dev->tx.tail = NULL;
    dev->tx.size = 0;
    vec_clear(&dev->rx_wait_queue);
}

int tty_gui_char_driver_init(void) {
    for (int i = 0; i < TTY_GUI_DEVICE_COUNT; i++) {
        kmemset(&tty_gui_devices[i], 0, sizeof(struct tty_gui_device));
        tty_gui_devices[i].refcount = 0;
        tty_gui_devices[i].active = 0;
    }

    return register_char_driver(&tty_gui_char_driver);
}

int tty_gui_create_device_nodes(void) {
    return 0;
}

int tty_gui_char_device_activate(int minor) {
    if (minor < 0 || minor >= TTY_GUI_DEVICE_COUNT) {
        return -1;
    }

    struct dev_st device_number = {
        .major = TTY_GUI_MAJOR,
        .minor = (uint16_t)minor,
    };
    err_t err = devfs_create_char_device(device_number);
    if (err) {
        return err;
    }

    if (tty_gui_devices[minor].active) {
        tty_gui_device_clear_buffers(&tty_gui_devices[minor]);
        return 0;
    }

    tty_gui_devices[minor].rx = create_ring_buffer(TTY_GUI_DEVICE_BUFFER_SIZE);
    tty_gui_devices[minor].tx = create_ring_buffer(TTY_GUI_DEVICE_BUFFER_SIZE);
    tty_gui_devices[minor].rx_wait_queue = vec_new(2, NULL);
    tty_gui_devices[minor].refcount = 0;
    tty_gui_devices[minor].active = 1;
    return 0;
}

void tty_gui_char_device_deactivate(int minor) {
    if (minor < 0 || minor >= TTY_GUI_DEVICE_COUNT ||
        !tty_gui_devices[minor].active) {
        return;
    }

    while (!vec_is_empty(&tty_gui_devices[minor].rx_wait_queue)) {
        void *tid_ptr;
        if (!vec_pop_back(&tty_gui_devices[minor].rx_wait_queue, &tid_ptr)) {
            continue;
        }
        tcb_t *thread = thread_get_by_tid((tid_t)(uintptr_t)tid_ptr);
        if (thread != NULL) {
            unblock_thread(thread);
        }
    }

    destroy_ring_buffer(&tty_gui_devices[minor].rx);
    destroy_ring_buffer(&tty_gui_devices[minor].tx);
    vec_destroy(&tty_gui_devices[minor].rx_wait_queue);
    kmemset(&tty_gui_devices[minor], 0, sizeof(struct tty_gui_device));
}

static int tty_gui_dev_open(struct oft_entry *entry) {
    struct tty_gui_device *dev = tty_gui_from_entry(entry, NULL);
    if (dev == NULL) {
        return -1;
    }

    dev->refcount++;
    return 0;
}

static int tty_gui_dev_close(struct oft_entry *entry) {
    struct tty_gui_device *dev = tty_gui_from_entry(entry, NULL);
    if (dev == NULL) {
        return -1;
    }

    if (dev->refcount > 0) {
        dev->refcount--;
    }
    return 0;
}

static int tty_gui_dev_read(struct oft_entry *entry, char *buffer, size_t count) {
    struct tty_gui_device *dev = tty_gui_from_entry(entry, NULL);
    if (dev == NULL || (buffer == NULL && count != 0)) {
        return -1;
    }

    size_t num_read = 0;
    while (num_read < count) {
        char next;
        if (consume_ring_buffer(&dev->rx, &next)) {
            buffer[num_read++] = next;
            continue;
        }

        if (num_read > 0) {
            return (int)num_read;
        }

        tcb_t *thread = get_curr_thread();
        if (thread == NULL) {
            return 0;
        }
        if (!wait_queue_has_tid(&dev->rx_wait_queue, thread->tid)) {
            vec_push_back(&dev->rx_wait_queue, (ptr_t)(uintptr_t)thread->tid);
        }
        block_thread(thread, THREAD_BLOCKED_INTERRUPTABLE);
    }

    return (int)num_read;
}

static int tty_gui_dev_write(struct oft_entry *entry, const char *buffer,
                             size_t count) {
    int minor;
    struct tty_gui_device *dev = tty_gui_from_entry(entry, &minor);
    if (dev == NULL || (buffer == NULL && count != 0)) {
        return -1;
    }

    size_t num_written = 0;
    while (num_written < count) {
        if (!produce_ring_buffer(&dev->tx, &buffer[num_written])) {
            break;
        }

        char next;
        if (consume_ring_buffer(&dev->tx, &next)) {
            tty_gui_write_char_for_tty(minor, next);
        }
        num_written++;
    }

    return (int)num_written;
}
