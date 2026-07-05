#pragma once

#include <stdint.h>

typedef struct gui_framebuffer_st {
    void *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t depth;
    uint32_t pixel_order;
    uint32_t size;
} gui_framebuffer_t;

int gui_framebuffer_init(void);
int gui_framebuffer_get(gui_framebuffer_t *fb);
uint32_t gui_framebuffer_encode_color(uint8_t red, uint8_t green,
                                      uint8_t blue);
int gui_framebuffer_clear(uint8_t red, uint8_t green, uint8_t blue);
int gui_framebuffer_put_pixel(uint32_t x, uint32_t y, uint8_t red,
                              uint8_t green, uint8_t blue);
uint32_t *gui_framebuffer_pixels32(void);

void gui_init_smoke_test(void);
