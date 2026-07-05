#include "gui_hw.h"

#include <stdint.h>

#include "memory/page_table/page_table.h"
#include "uart/uart.h"

#define GUI_FB_WIDTH 640u
#define GUI_FB_HEIGHT 480u
#define GUI_FB_DEPTH 32u

#define QEMU_RPI3_MAILBOX_BASE UINT64_C(0x3f00b880)
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

#define PIXEL_ORDER_RGB 1u

static volatile uint32_t mbox_buffer[48] __attribute__((aligned(16)));

static uint32_t mmio_read(uint32_t offset) {
    return rpi5_mmio_read32(QEMU_RPI3_MAILBOX_BASE + offset);
}

static void mmio_write(uint32_t offset, uint32_t value) {
    rpi5_mmio_write32(QEMU_RPI3_MAILBOX_BASE + offset, value);
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

int gui_hw_framebuffer_init(gui_framebuffer_t *fb) {
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
