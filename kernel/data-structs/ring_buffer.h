#pragma once

#include "memory/kmalloc.h"

struct RingBuffer {
    char *head;
    char *tail;
    char *data;
    int capacity;
    int size;
};

struct RingBuffer create_ring_buffer(int capacity);
bool consume_ring_buffer(struct RingBuffer *buf, char *next);
bool produce_ring_buffer(struct RingBuffer *buf, char *next);
int destroy_ring_buffer(struct RingBuffer *buf);
bool remove_back_ring_buffer(struct RingBuffer *buf);
