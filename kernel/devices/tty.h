
#define MAX_TTY 16

struct tty_device {
    uint32_t minor;
    char name[32];
    dev_t device_number;

    struct ring_buffer rx;
    struct ring_buffer tx;

    Vec rx_wait_queue;
    Vec tx_wait_queue;

    int refcount;
};

struct tty_driver_state {
    struct tty_device *devices[MAX_TTY_DEVICES];
    uint16_t num_ttys;
    // TODO add lock
}

void tty_send_input(int minor, const void *buffer, size_t count);
