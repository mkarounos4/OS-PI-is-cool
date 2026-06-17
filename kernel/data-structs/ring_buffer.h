#pragma once

struct RingBuffer {
    void *head;
    void *tail;
    void *data[];
    int capacity;
    int size;
};

struct RingBuffer *create_ring_buffer(int capacity);
bool consume_ring_buffer(struct RingBuffer *buf, void *next);
bool produce_ring_buffer(struct RingBuffer *buf, void *next);
