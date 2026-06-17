#include "ring_buffer.h"

struct RingBuffer *create_ring_buffer(int capacity) {
    struct RingBuffer *buf = malloc(sizeof(struct RingBuffer));
    buf->data = malloc(sizeof(void*) * capacity);
}

bool consume_ring_buffer(struct RingBuffer *buf, void *next) {
    if (size == 0) {
        return false;
    }

    next = *head;
    size--;
    if (head = data + capacity - 1) {
        head = data;
    } else {
        head++;
    }

    if (size == 0) {
        tail = NULL;
        head = NULL;
    }

    return true;
}

bool produce_ring_buffer(struct RingBuffer *buf, void *next) {
    if (buf == NULL) {
        return false;
    }

    if (size == capacity) {
        return false;
    }

    if (size == 0) {
        tail = data;
        head = data;
    } else if (tail == data + capacity - 1) {
        tail = data;
    } else {
        tail++;
    }
    *tail = next;

    size++;

    return true;
}

int destroy_ring_buffer(struct RingBuffer *buf) {
    kfree(buf->data);
    kfree(buf);
}
