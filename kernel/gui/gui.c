#include "gui.h"

#include <stdint.h>

#include "gui_hw.h"
#include "uart/uart.h"

#define GUI_FB_SMOKE_RED 0xffu
#define GUI_FB_SMOKE_GREEN 0x00u
#define GUI_FB_SMOKE_BLUE 0xffu

#ifdef PLATFORM_QEMU
#define PIXEL_ORDER_BGR 1u
#else
#define PIXEL_ORDER_BGR 0u
#endif

static gui_framebuffer_t active_fb;
static int active_fb_ready;

static void framebuffer_fill32(gui_framebuffer_t *fb, uint32_t color) {
    uint8_t *row = (uint8_t *)fb->addr;

    for (uint32_t y = 0; y < fb->height; y++) {
        uint32_t *pixel = (uint32_t *)row;
        for (uint32_t x = 0; x < fb->width; x++) {
            pixel[x] = color;
        }
        row += fb->pitch;
    }
}

static void framebuffer_fill16(gui_framebuffer_t *fb, uint16_t color) {
    uint8_t *row = (uint8_t *)fb->addr;

    for (uint32_t y = 0; y < fb->height; y++) {
        uint16_t *pixel = (uint16_t *)row;
        for (uint32_t x = 0; x < fb->width; x++) {
            pixel[x] = color;
        }
        row += fb->pitch;
    }
}

int gui_framebuffer_init(void) {
    if (active_fb_ready) {
        return 1;
    }

    if (!gui_hw_framebuffer_init(&active_fb)) {
        return 0;
    }

    active_fb_ready = 1;
    return 1;
}

int gui_framebuffer_get(gui_framebuffer_t *fb) {
    if (fb == 0 || !active_fb_ready) {
        return 0;
    }

    *fb = active_fb;
    return 1;
}

uint32_t gui_framebuffer_encode_color(uint8_t red, uint8_t green,
                                      uint8_t blue) {
    if (active_fb_ready && active_fb.depth == 16) {
        return (((uint32_t)red >> 3) << 11) |
               (((uint32_t)green >> 2) << 5) |
               ((uint32_t)blue >> 3);
    }

    if (active_fb_ready && active_fb.pixel_order == PIXEL_ORDER_BGR) {
        return ((uint32_t)blue << 16) |
               ((uint32_t)green << 8) |
               (uint32_t)red;
    }

    return ((uint32_t)red << 16) |
           ((uint32_t)green << 8) |
           (uint32_t)blue;
}

static void framebuffer_fill(gui_framebuffer_t *fb, uint32_t color) {
    if (fb->depth == 16) {
        framebuffer_fill16(fb, (uint16_t)color);
        return;
    }

    framebuffer_fill32(fb, color);
}

int gui_framebuffer_clear(uint8_t red, uint8_t green, uint8_t blue) {
    if (!active_fb_ready && !gui_framebuffer_init()) {
        return 0;
    }

    framebuffer_fill(&active_fb, gui_framebuffer_encode_color(red, green,
                                                              blue));
    return 1;
}

int gui_framebuffer_put_pixel_encoded(uint32_t x, uint32_t y, uint32_t color) {
    if (!active_fb_ready && !gui_framebuffer_init()) {
        return 0;
    }
    if (x >= active_fb.width || y >= active_fb.height) {
        return 0;
    }

    uint8_t *row = (uint8_t *)active_fb.addr + ((uint64_t)y * active_fb.pitch);
    if (active_fb.depth == 16) {
        ((uint16_t *)row)[x] = (uint16_t)color;
    } else {
        ((uint32_t *)row)[x] = color;
    }

    return 1;
}

int gui_framebuffer_put_pixel(uint32_t x, uint32_t y, uint8_t red,
                              uint8_t green, uint8_t blue) {
    uint32_t color = gui_framebuffer_encode_color(red, green, blue);
    return gui_framebuffer_put_pixel_encoded(x, y, color);
}

int gui_framebuffer_get_pixel(uint32_t x, uint32_t y, uint32_t *color) {
    if (!active_fb_ready && !gui_framebuffer_init()) {
        return 0;
    }
    if (x >= active_fb.width || y >= active_fb.height) {
        return 0;
    }

    uint8_t *row = (uint8_t *)active_fb.addr + ((uint64_t)y * active_fb.pitch);
    *color = row[x];
    return 1;
}

uint32_t *gui_framebuffer_pixels32(void) {
    if (!active_fb_ready || active_fb.depth != 32) {
        return 0;
    }

    return (uint32_t *)active_fb.addr;
}

void gui_init_smoke_test(void) {
    gui_framebuffer_t fb;

    printf("[gui] framebuffer init begin\n");
    if (!gui_framebuffer_init() || !gui_framebuffer_get(&fb)) {
        printf("[gui] framebuffer init failed\n");
        return;
    }

    gui_framebuffer_clear(GUI_FB_SMOKE_RED, GUI_FB_SMOKE_GREEN,
                          GUI_FB_SMOKE_BLUE);
    printf("[gui] framebuffer filled width=");
    printf("%u", fb.width);
    printf(" height=");
    printf("%u", fb.height);
    printf(" pitch=");
    printf("%u", fb.pitch);
    printf(" depth=");
    printf("%u", fb.depth);
    printf(" order=");
    printf("%u", fb.pixel_order);
    printf(" color=0xff00ff\n");

    printf("drawing smaller square\n");

    const gui_framebuffer_t *fb2 = &active_fb;
    for (uint32_t y = fb2->height / 4; y < fb2->height / 4 * 3; y++) {
        for (uint32_t x = fb2->width / 4; x < fb2->width / 4 * 3; x++) {
            gui_framebuffer_put_pixel(x, y, 0x0, 0xff, 0xff);
        }
    }
}
