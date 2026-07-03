#include "ring_buffer.h"
#include "uart.h"

struct RingBuffer create_ring_buffer(int capacity) {
    struct RingBuffer buf;
    buf.head = NULL;
    buf.tail = NULL;
    buf.size = 0;
    buf.capacity = capacity;
    buf.data = kmalloc(sizeof(char*) * capacity);
    return buf;
}

bool consume_ring_buffer(struct RingBuffer *buf, char *next) {
    if (buf->size == 0) {
        return false;
    }

    *next = *(buf->head);
    buf->size--;
    if (buf->head == buf->data + buf->capacity - 1) {
        buf->head = buf->data;
    } else {
        buf->head++;
    }

    if (buf->size == 0) {
        buf->tail = NULL;
        buf->head = NULL;
    }

    return true;
}

bool produce_ring_buffer(struct RingBuffer *buf, const char *next) {
    if (buf == NULL) {
        return false;
    }

    if (buf->size == buf->capacity) {
        return false;
    }

    if (buf->size == 0) {
        buf->tail = buf->data;
        buf->head = buf->data;
    } else if (buf->tail == buf->data + buf->capacity - 1) {
        buf->tail = buf->data;
    } else {
        buf->tail++;
    }
    *(buf->tail) = *next;

    buf->size++;

    return true;
}

int destroy_ring_buffer(struct RingBuffer *buf) {
    kfree(buf->data);
    return 0;
}

bool remove_back_ring_buffer(struct RingBuffer *buf) {
    if (buf == NULL) {
        return false;
    }

    if (buf->size == 0) {
        return false;
    }

    buf->size--;
    if (buf->tail == buf->data) {
        buf->tail = buf->data + buf->capacity - 1;
    } else {
        buf->tail--;
    }

    if (buf->size == 0) {
        buf->tail = NULL;
        buf->head = NULL;
    }
    
    return true;
}
