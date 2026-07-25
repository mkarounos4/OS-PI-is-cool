#include "usb_keyboard_device.h"

#include <stdint.h>

#include "data-structs/ring_buffer.h"
#include "data-structs/vec.h"
#include "devices/devices.h"
#include "devices/tty.h"
#include "scheduler/scheduler.h"
#include "threading/thread.h"
#include "uart/uart.h"

#define USB_KEYBOARD_BUFFER_SIZE 4096
#define USB_KEYBOARD_DEVICE_COUNT 1

struct usb_keyboard_device {
    struct RingBuffer rx;
    Vec rx_wait_queue;
    int refcount;
    uint8_t active;
};

static struct usb_keyboard_device keyboard_devices[USB_KEYBOARD_DEVICE_COUNT];
static uint64_t bytes_received;
static uint64_t bytes_read;

static struct usb_keyboard_device *keyboard_from_entry(struct oft_entry *entry) {
    if (entry == NULL || entry->inode == NULL) {
        return NULL;
    }
    uint16_t minor = entry->inode->inode.metadata.i_rdev.minor;
    if (minor >= USB_KEYBOARD_DEVICE_COUNT ||
        !keyboard_devices[minor].active) {
        return NULL;
    }
    return &keyboard_devices[minor];
}

static int wait_queue_has_tid(Vec *queue, tid_t tid) {
    for (size_t i = 0; i < vec_len(queue); i++) {
        if ((tid_t)(uintptr_t)vec_get(queue, i) == tid) {
            return 1;
        }
    }
    return 0;
}

static void wake_readers(struct usb_keyboard_device *device) {
    while (!vec_is_empty(&device->rx_wait_queue)) {
        void *tid_pointer;
        if (!vec_pop_back(&device->rx_wait_queue, &tid_pointer)) {
            continue;
        }
        tcb_t *thread = thread_get_by_tid((tid_t)(uintptr_t)tid_pointer);
        if (thread != NULL) {
            unblock_thread(thread);
        }
    }
}

static int keyboard_open(struct oft_entry *entry) {
    struct usb_keyboard_device *device = keyboard_from_entry(entry);
    if (device == NULL) {
        return -1;
    }
    device->refcount++;
    return 0;
}

static int keyboard_close(struct oft_entry *entry) {
    struct usb_keyboard_device *device = keyboard_from_entry(entry);
    if (device == NULL) {
        return -1;
    }
    if (device->refcount > 0) {
        device->refcount--;
    }
    return 0;
}

static int keyboard_read(struct oft_entry *entry, char *buffer, size_t count) {
    struct usb_keyboard_device *device = keyboard_from_entry(entry);
    if (device == NULL || (buffer == NULL && count != 0)) {
        return -1;
    }

    size_t read = 0;
    while (read < count) {
        if (consume_ring_buffer(&device->rx, &buffer[read])) {
            read++;
            continue;
        }
        if (read != 0) {
            break;
        }
        tcb_t *thread = get_curr_thread();
        if (thread == NULL) {
            break;
        }
        if (!wait_queue_has_tid(&device->rx_wait_queue, thread->tid)) {
            vec_push_back(&device->rx_wait_queue,
                          (ptr_t)(uintptr_t)thread->tid);
        }
        block_thread(thread, THREAD_BLOCKED_INTERRUPTABLE);
    }
    if (read != 0) {
        bytes_read += read;
    }
    return (int)read;
}

static int keyboard_write(struct oft_entry *entry, const char *buffer,
                          size_t count) {
    (void)entry;
    (void)buffer;
    (void)count;
    return -1;
}

static const struct file_operations keyboard_fops = {
    .open = keyboard_open,
    .close = keyboard_close,
    .read = keyboard_read,
    .write = keyboard_write,
};

static struct char_driver keyboard_driver = {
    .name = "usb",
    .major = USB_KEYBOARD_MAJOR,
    .fops = &keyboard_fops,
    .driver_data = keyboard_devices,
};

int usb_keyboard_char_driver_init(void) {
    for (unsigned i = 0; i < USB_KEYBOARD_DEVICE_COUNT; i++) {
        keyboard_devices[i].rx =
            create_ring_buffer(USB_KEYBOARD_BUFFER_SIZE);
        keyboard_devices[i].rx_wait_queue = vec_new(2, NULL);
        keyboard_devices[i].refcount = 0;
        keyboard_devices[i].active = 1;
    }
    return register_char_driver(&keyboard_driver);
}

int usb_keyboard_create_device_nodes(void) {
    struct dev_st device = {
        .major = USB_KEYBOARD_MAJOR,
        .minor = 0,
    };
    return devfs_create_char_device(device);
}

void usb_keyboard_device_receive(const char *buffer, size_t count) {
    if (buffer == NULL || !keyboard_devices[0].active) {
        return;
    }

    size_t produced = 0;
    while (produced < count &&
           produce_ring_buffer(&keyboard_devices[0].rx, &buffer[produced])) {
        produced++;
    }
    if (produced == 0) {
        uart_puts("[usb] input buffer full; dropping bytes\n");
        return;
    }

    bytes_received += produced;

    wake_readers(&keyboard_devices[0]);
    struct dev_st input = {
        .major = USB_KEYBOARD_MAJOR,
        .minor = 0,
    };
    tty_receive_input_from_device(input, produced);
}

void usb_keyboard_get_counters(uint64_t *received, uint64_t *read) {
    if (received != NULL) {
        *received = bytes_received;
    }
    if (read != NULL) {
        *read = bytes_read;
    }
}
