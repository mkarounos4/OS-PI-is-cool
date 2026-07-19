#include "uart_device.h"

#include "uart.h"
#include "devices/devices.h"
#include "devices/tty.h"
#include "scheduler/scheduler.h"
#include "threading/thread.h"

#define UART_DEVICE_BUFFER_SIZE 4096
#define UART_DEVICE_COUNT 1

struct uart_device {
    struct RingBuffer rx;
    struct RingBuffer tx;
    Vec rx_wait_queue;
    int refcount;
    uint8_t active;
};

static struct uart_device uart_devices[UART_DEVICE_COUNT];

static int uart_dev_open(struct oft_entry *entry);
static int uart_dev_close(struct oft_entry *entry);
static int uart_dev_read(struct oft_entry *entry, char *buffer, size_t count);
static int uart_dev_write(struct oft_entry *entry, const char *buffer, size_t count);

static const struct file_operations uart_fops = {
    .open = uart_dev_open,
    .close = uart_dev_close,
    .read = uart_dev_read,
    .write = uart_dev_write,
};

static struct char_driver uart_char_driver = {
    .name = "uart",
    .major = UART_MAJOR,
    .fops = &uart_fops,
    .driver_data = uart_devices,
};

static struct uart_device *uart_from_entry(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return NULL;
    }

    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= UART_DEVICE_COUNT || !uart_devices[minor].active) {
        return NULL;
    }

    return &uart_devices[minor];
}

static int wait_queue_has_tid(Vec *queue, tid_t tid) {
    for (size_t i = 0; i < vec_len(queue); i++) {
        if ((tid_t)(uintptr_t)vec_get(queue, i) == tid) {
            return 1;
        }
    }
    return 0;
}

static void wake_uart_readers(struct uart_device *dev) {
    while (!vec_is_empty(&dev->rx_wait_queue)) {
        void *tid_ptr;
        if (!vec_pop_back(&dev->rx_wait_queue, &tid_ptr)) {
            continue;
        }

        tcb_t *thread = thread_get_by_tid((tid_t)(uintptr_t)tid_ptr);
        if (thread != NULL) {
            unblock_thread(thread);
        }
    }
}

static void uart_emit_char(char ch) {
    if (ch == '\n') {
        uart_raw_putc('\r');
    }
    uart_raw_putc(ch);
}

int uart_char_driver_init(void) {
    for (int i = 0; i < UART_DEVICE_COUNT; i++) {
        uart_devices[i].rx = create_ring_buffer(UART_DEVICE_BUFFER_SIZE);
        uart_devices[i].tx = create_ring_buffer(UART_DEVICE_BUFFER_SIZE);
        uart_devices[i].rx_wait_queue = vec_new(2, NULL);
        uart_devices[i].refcount = 0;
        uart_devices[i].active = 1;
    }

    return register_char_driver(&uart_char_driver);
}

int uart_create_device_nodes(void) {
    struct dev_st device_number = {
        .major = UART_MAJOR,
        .minor = 0,
    };
    return devfs_create_char_device(device_number);
}

void uart_char_device_receive(const char *buffer, size_t count) {
    if (buffer == NULL || !uart_devices[0].active) {
        return;
    }

    size_t produced = 0;
    while (produced < count) {
        if (!produce_ring_buffer(&uart_devices[0].rx, &buffer[produced])) {
            break;
        }
        produced++;
    }

    if (produced == 0) {
        return;
    }

    wake_uart_readers(&uart_devices[0]);

    struct dev_st input = {
        .major = UART_MAJOR,
        .minor = 0,
    };
    tty_receive_input_from_device(input, produced);
}

static int uart_dev_open(struct oft_entry *entry) {
    struct uart_device *dev = uart_from_entry(entry);
    if (dev == NULL) {
        return -1;
    }

    dev->refcount++;
    return 0;
}

static int uart_dev_close(struct oft_entry *entry) {
    struct uart_device *dev = uart_from_entry(entry);
    if (dev == NULL) {
        return -1;
    }

    if (dev->refcount > 0) {
        dev->refcount--;
    }
    return 0;
}

static int uart_dev_read(struct oft_entry *entry, char *buffer, size_t count) {
    struct uart_device *dev = uart_from_entry(entry);
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

static int uart_dev_write(struct oft_entry *entry, const char *buffer, size_t count) {
    struct uart_device *dev = uart_from_entry(entry);
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
            uart_emit_char(next);
        }
        num_written++;
    }

    return (int)num_written;
}

