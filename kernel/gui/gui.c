#include "gui.h"

#include <stdint.h>

#include "memory/page_table/page_table.h"
#include "uart/uart.h"

#define GUI_FB_WIDTH 1920u
#define GUI_FB_HEIGHT 1080u
#define GUI_FB_DEPTH 32u
#define GUI_FB_SMOKE_RED 0xffu
#define GUI_FB_SMOKE_GREEN 0x00u
#define GUI_FB_SMOKE_BLUE 0xffu

#define RPI5_MAILBOX_BASE UINT64_C(0x107c013880)
#define MBOX_READ 0x00u
#define MBOX_STATUS 0x18u
#define MBOX_WRITE 0x20u

#define MBOX_STATUS_FULL 0x80000000u
#define MBOX_STATUS_EMPTY 0x40000000u
#define MBOX_CHANNEL_PROPERTY 8u
#define MBOX_RESPONSE_SUCCESS 0x80000000u
#define MBOX_REQUEST 0u
#define MBOX_POLL_LIMIT 0x1000000u

#define TAG_GET_PHYSICAL_SIZE 0x00040003u
#define TAG_SET_PHYSICAL_SIZE 0x00048003u
#define TAG_GET_VIRTUAL_SIZE 0x00040004u
#define TAG_SET_VIRTUAL_SIZE 0x00048004u
#define TAG_GET_DEPTH 0x00040005u
#define TAG_SET_DEPTH 0x00048005u
#define TAG_SET_PIXEL_ORDER 0x00048006u
#define TAG_ALLOCATE_BUFFER 0x00040001u
#define TAG_GET_PIXEL_ORDER 0x00040006u
#define TAG_GET_PITCH 0x00040008u
#define TAG_LAST 0u

#define PIXEL_ORDER_BGR 0u
#define PIXEL_ORDER_RGB 1u

static volatile uint32_t mbox_buffer[48] __attribute__((aligned(16)));
static gui_framebuffer_t active_fb;
static int active_fb_ready;

static uint32_t mmio_read(uint32_t offset) {
    return rpi5_mmio_read32(RPI5_MAILBOX_BASE + offset);
}

static void mmio_write(uint32_t offset, uint32_t value) {
    rpi5_mmio_write32(RPI5_MAILBOX_BASE + offset, value);
}

static int mailbox_call(uint32_t channel) {
    uint32_t message =
        (uint32_t)(kernel_phys_addr((uint64_t)(uintptr_t)mbox_buffer) |
                   channel);

    for (uint32_t i = 0; i < MBOX_POLL_LIMIT; i++) {
        if ((mmio_read(MBOX_STATUS) & MBOX_STATUS_FULL) == 0) {
            mmio_write(MBOX_WRITE, message);
            break;
        }
        if (i + 1 == MBOX_POLL_LIMIT) {
            return 0;
        }
    }

    for (uint32_t i = 0; i < MBOX_POLL_LIMIT; i++) {
        if ((mmio_read(MBOX_STATUS) & MBOX_STATUS_EMPTY) != 0) {
            continue;
        }

        uint32_t response = mmio_read(MBOX_READ);
        if (response == message) {
            return mbox_buffer[1] == MBOX_RESPONSE_SUCCESS;
        }
    }

    return 0;
}

static uint64_t framebuffer_bus_to_cpu(uint32_t bus_addr) {
    return (uint64_t)(bus_addr & 0x3fffffffu);
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static int __attribute__((unused)) mailbox_framebuffer_init(gui_framebuffer_t *fb) {
    if (fb == 0) {
        return 0;
    }

    mbox_buffer[0] = sizeof(mbox_buffer);
    mbox_buffer[1] = MBOX_REQUEST;

    mbox_buffer[2] = TAG_SET_PHYSICAL_SIZE;
    mbox_buffer[3] = 8;
    mbox_buffer[4] = 0;
    mbox_buffer[5] = GUI_FB_WIDTH;
    mbox_buffer[6] = GUI_FB_HEIGHT;

    mbox_buffer[7] = TAG_SET_VIRTUAL_SIZE;
    mbox_buffer[8] = 8;
    mbox_buffer[9] = 0;
    mbox_buffer[10] = GUI_FB_WIDTH;
    mbox_buffer[11] = GUI_FB_HEIGHT;

    mbox_buffer[12] = TAG_SET_DEPTH;
    mbox_buffer[13] = 4;
    mbox_buffer[14] = 0;
    mbox_buffer[15] = GUI_FB_DEPTH;

    mbox_buffer[16] = TAG_SET_PIXEL_ORDER;
    mbox_buffer[17] = 4;
    mbox_buffer[18] = 0;
    mbox_buffer[19] = PIXEL_ORDER_RGB;

    mbox_buffer[20] = TAG_ALLOCATE_BUFFER;
    mbox_buffer[21] = 8;
    mbox_buffer[22] = 0;
    mbox_buffer[23] = 16;
    mbox_buffer[24] = 0;

    mbox_buffer[25] = TAG_GET_PIXEL_ORDER;
    mbox_buffer[26] = 4;
    mbox_buffer[27] = 0;
    mbox_buffer[28] = 0;

    mbox_buffer[29] = TAG_GET_PHYSICAL_SIZE;
    mbox_buffer[30] = 8;
    mbox_buffer[31] = 0;
    mbox_buffer[32] = 0;
    mbox_buffer[33] = 0;

    mbox_buffer[34] = TAG_GET_VIRTUAL_SIZE;
    mbox_buffer[35] = 8;
    mbox_buffer[36] = 0;
    mbox_buffer[37] = 0;
    mbox_buffer[38] = 0;

    mbox_buffer[39] = TAG_GET_DEPTH;
    mbox_buffer[40] = 4;
    mbox_buffer[41] = 0;
    mbox_buffer[42] = 0;

    mbox_buffer[43] = TAG_GET_PITCH;
    mbox_buffer[44] = 4;
    mbox_buffer[45] = 0;
    mbox_buffer[46] = 0;

    mbox_buffer[47] = TAG_LAST;

    if (!mailbox_call(MBOX_CHANNEL_PROPERTY)) {
        return 0;
    }

    uint32_t fb_bus_addr = mbox_buffer[23];
    uint32_t fb_size = mbox_buffer[24];
    uint32_t pixel_order = mbox_buffer[28];
    uint32_t returned_width = mbox_buffer[32];
    uint32_t returned_height = mbox_buffer[33];
    uint32_t virtual_width = mbox_buffer[37];
    uint32_t virtual_height = mbox_buffer[38];
    uint32_t depth = mbox_buffer[42];
    uint32_t pitch = mbox_buffer[46];

    if (returned_width == 0 || returned_height == 0 ||
        virtual_width == 0 || virtual_height == 0 ||
        (depth != 16 && depth != 32) ||
        fb_bus_addr == 0 || fb_size == 0 || pitch == 0) {
        return 0;
    }

    uint32_t bytes_per_pixel = depth / 8;
    uint32_t pitch_width = pitch / bytes_per_pixel;
    uint32_t buffer_height = fb_size / pitch;

    uint64_t fb_cpu_pa = framebuffer_bus_to_cpu(fb_bus_addr);
    fb->addr = (void *)(uintptr_t)kernel_direct_map_va(fb_cpu_pa);
    fb->width = max_u32(virtual_width, pitch_width);
    fb->height = max_u32(virtual_height, buffer_height);
    fb->pitch = pitch;
    fb->depth = depth;
    fb->pixel_order = pixel_order;
    fb->size = fb_size;
    return 1;
}

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
#ifdef PLATFORM_RPI5
    if (active_fb_ready) {
        return 1;
    }

    if (!mailbox_framebuffer_init(&active_fb)) {
        return 0;
    }

    active_fb_ready = 1;
    return 1;
#else
    return 0;
#endif
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

int gui_framebuffer_put_pixel(uint32_t x, uint32_t y, uint8_t red,
                              uint8_t green, uint8_t blue) {
    if (!active_fb_ready && !gui_framebuffer_init()) {
        return 0;
    }
    if (x >= active_fb.width || y >= active_fb.height) {
        return 0;
    }

    uint8_t *row = (uint8_t *)active_fb.addr + ((uint64_t)y * active_fb.pitch);
    uint32_t color = gui_framebuffer_encode_color(red, green, blue);
    if (active_fb.depth == 16) {
        ((uint16_t *)row)[x] = (uint16_t)color;
    } else {
        ((uint32_t *)row)[x] = color;
    }

    return 1;
}

uint32_t *gui_framebuffer_pixels32(void) {
    if (!active_fb_ready || active_fb.depth != 32) {
        return 0;
    }

    return (uint32_t *)active_fb.addr;
}

void gui_init_smoke_test(void) {
#ifdef PLATFORM_RPI5
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
    
#endif
}
