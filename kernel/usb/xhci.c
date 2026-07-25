#include "xhci.h"

#include <stddef.h>
#include <stdint.h>

#include "hid_keyboard.h"
#include "usb_keyboard_device.h"
#include "timer/timer.h"
#include "uart/rpi5_addresses.h"
#include "uart/uart.h"

#ifdef PLATFORM_RPI5

#define XHCI_CONTROLLER_COUNT 2
#define XHCI_MAX_SLOTS         8
#define XHCI_MAX_PORTS         8
#define XHCI_RING_TRBS         32
#define XHCI_EVENT_TRBS        64
#define XHCI_MAX_SCRATCHPADS    8
#define XHCI_DMA_OFFSET        UINT64_C(0x1000000000)
#define KERNEL_VA_BASE         UINT64_C(0xffff000000000000)

#define USBCMD_RUN             (1u << 0)
#define USBCMD_RESET           (1u << 1)
#define USBSTS_HALTED          (1u << 0)
#define USBSTS_CNR             (1u << 11)
#define PORTSC_CCS             (1u << 0)
#define PORTSC_PED             (1u << 1)
#define PORTSC_PR              (1u << 4)
#define PORTSC_PP              (1u << 9)
#define PORTSC_SPEED_SHIFT     10
#define PORTSC_CHANGE_BITS     (0x7fu << 17)

#define TRB_CYCLE              (1u << 0)
#define TRB_TOGGLE_CYCLE       (1u << 1)
#define TRB_IOC                (1u << 5)
#define TRB_IDT                (1u << 6)
#define TRB_DIR_IN             (1u << 16)
#define TRB_TYPE(n)            ((uint32_t)(n) << 10)
#define TRB_TYPE_GET(v)        (((v) >> 10) & 0x3fu)
#define TRB_SLOT_SHIFT         24

#define TRB_NORMAL             1
#define TRB_SETUP              2
#define TRB_DATA               3
#define TRB_STATUS             4
#define TRB_LINK               6
#define TRB_ENABLE_SLOT        9
#define TRB_DISABLE_SLOT       10
#define TRB_ADDRESS_DEVICE     11
#define TRB_CONFIGURE_EP       12
#define TRB_EVALUATE_CONTEXT   13
#define TRB_TRANSFER_EVENT     32
#define TRB_COMMAND_EVENT      33

#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_CONFIG     9
#define USB_REQ_SET_PROTOCOL   11
#define USB_DESC_DEVICE        1
#define USB_DESC_CONFIG        2
#define USB_DESC_INTERFACE     4
#define USB_DESC_ENDPOINT      5

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

struct xhci_erst_entry {
    uint64_t address;
    uint32_t size;
    uint32_t reserved;
} __attribute__((packed));

struct xhci_dma_area {
    uint64_t dcbaa[XHCI_MAX_SLOTS + 1] __attribute__((aligned(64)));
    struct xhci_trb command[XHCI_RING_TRBS] __attribute__((aligned(64)));
    struct xhci_trb events[XHCI_EVENT_TRBS] __attribute__((aligned(64)));
    struct xhci_erst_entry erst __attribute__((aligned(64)));
    uint64_t scratchpad_array[XHCI_MAX_SCRATCHPADS]
        __attribute__((aligned(64)));
    uint8_t scratchpad_pages[XHCI_MAX_SCRATCHPADS][4096]
        __attribute__((aligned(4096)));
    uint8_t output_context[2048] __attribute__((aligned(64)));
    uint8_t input_context[2112] __attribute__((aligned(64)));
    struct xhci_trb ep0[XHCI_RING_TRBS] __attribute__((aligned(64)));
    struct xhci_trb interrupt[XHCI_RING_TRBS] __attribute__((aligned(64)));
    uint8_t descriptor[256] __attribute__((aligned(64)));
    uint8_t report[8] __attribute__((aligned(64)));
} __attribute__((aligned(4096)));

_Static_assert(offsetof(struct xhci_dma_area, command) % 64 == 0,
               "xHCI command ring must be 64-byte aligned");
_Static_assert(offsetof(struct xhci_dma_area, events) % 64 == 0,
               "xHCI event ring must be 64-byte aligned");
_Static_assert(offsetof(struct xhci_dma_area, erst) % 64 == 0,
               "xHCI ERST must be 64-byte aligned");

struct xhci_ring {
    struct xhci_trb *trbs;
    unsigned enqueue;
    unsigned cycle;
};

struct xhci_controller {
    uint64_t base;
    uint64_t op;
    uint64_t runtime;
    uint64_t doorbells;
    unsigned context_size;
    unsigned max_ports;
    unsigned event_dequeue;
    unsigned event_cycle;
    unsigned slot;
    unsigned port;
    unsigned speed;
    unsigned interrupt_dci;
    unsigned interrupt_packet;
    struct xhci_ring command;
    struct xhci_ring ep0;
    struct xhci_ring interrupt;
    struct xhci_dma_area *dma;
    struct hid_keyboard keyboard;
    uint64_t report_trb;
    uint8_t running;
    uint8_t keyboard_ready;
    uint8_t report_pending;
    uint8_t first_report_logged;
    uint8_t enumeration_attempts;
    uint8_t connected_mask;
    uint32_t poll_count;
    uint32_t event_count;
    uint32_t report_count;
    uint32_t last_usbsts;
    uint32_t last_portsc;
    uint32_t portsc[XHCI_MAX_PORTS];
    uint32_t capability_word0;
    uint32_t hcsparams1;
    uint32_t hccparams1;
    uint8_t capability_length;
    uint16_t hci_version;
    const char *stage;
};

static struct xhci_dma_area dma_areas[XHCI_CONTROLLER_COUNT]
    __attribute__((aligned(4096)));
static struct xhci_controller controllers[XHCI_CONTROLLER_COUNT];

static inline uint64_t mmio_va(uint64_t address) {
    return KERNEL_VA_BASE | (address & UINT64_C(0x0000ffffffffffff));
}

static inline uint8_t mmio_read8(uint64_t address) {
    return *(volatile uint8_t *)(uintptr_t)mmio_va(address);
}

static inline uint16_t mmio_read16(uint64_t address) {
    return *(volatile uint16_t *)(uintptr_t)mmio_va(address);
}

static inline uint32_t mmio_read32(uint64_t address) {
    return *(volatile uint32_t *)(uintptr_t)mmio_va(address);
}

static inline void mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)mmio_va(address) = value;
    asm volatile("dsb sy" ::: "memory");
}

static inline void mmio_write64(uint64_t address, uint64_t value) {
    mmio_write32(address, (uint32_t)value);
    mmio_write32(address + 4, (uint32_t)(value >> 32));
}

static uint64_t dma_address(const void *pointer) {
    uint64_t va = (uint64_t)(uintptr_t)pointer;
    uint64_t pa = va >= KERNEL_VA_BASE ? va - KERNEL_VA_BASE : va;
    return pa + XHCI_DMA_OFFSET;
}

static void cache_clean(const void *pointer, size_t size) {
    uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)63;
    uintptr_t end = ((uintptr_t)pointer + size + 63) & ~(uintptr_t)63;
    for (uintptr_t p = start; p < end; p += 64) {
        asm volatile("dc cvac, %0" : : "r"(p) : "memory");
    }
    asm volatile("dsb sy" ::: "memory");
}

static void cache_invalidate(const void *pointer, size_t size) {
    uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)63;
    uintptr_t end = ((uintptr_t)pointer + size + 63) & ~(uintptr_t)63;
    asm volatile("dsb sy" ::: "memory");
    for (uintptr_t p = start; p < end; p += 64) {
        asm volatile("dc ivac, %0" : : "r"(p) : "memory");
    }
    asm volatile("dsb sy" ::: "memory");
}

static void zero_bytes(void *pointer, size_t size) {
    uint8_t *p = pointer;
    while (size-- != 0) {
        *p++ = 0;
    }
}

static int wait_bits(uint64_t address, uint32_t mask, uint32_t wanted,
                     unsigned milliseconds) {
    while (milliseconds-- != 0) {
        if ((mmio_read32(address) & mask) == wanted) {
            return 0;
        }
        timer_delay_ms(1);
    }
    return -1;
}

static void ring_init(struct xhci_ring *ring, struct xhci_trb *trbs) {
    zero_bytes(trbs, sizeof(*trbs) * XHCI_RING_TRBS);
    ring->trbs = trbs;
    ring->enqueue = 0;
    ring->cycle = 1;
    trbs[XHCI_RING_TRBS - 1].parameter = dma_address(trbs);
    trbs[XHCI_RING_TRBS - 1].control =
        TRB_TYPE(TRB_LINK) | TRB_TOGGLE_CYCLE | TRB_CYCLE;
    cache_clean(trbs, sizeof(*trbs) * XHCI_RING_TRBS);
}

static uint64_t ring_push(struct xhci_ring *ring, uint64_t parameter,
                          uint32_t status, uint32_t control) {
    struct xhci_trb *trb = &ring->trbs[ring->enqueue];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (ring->cycle ? TRB_CYCLE : 0);
    cache_clean(trb, sizeof(*trb));
    uint64_t address = dma_address(trb);

    ring->enqueue++;
    if (ring->enqueue == XHCI_RING_TRBS - 1) {
        struct xhci_trb *link = &ring->trbs[XHCI_RING_TRBS - 1];
        link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE_CYCLE |
                        (ring->cycle ? TRB_CYCLE : 0);
        cache_clean(link, sizeof(*link));
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }
    return address;
}

static int next_event(struct xhci_controller *hc, struct xhci_trb *event) {
    struct xhci_trb *source = &hc->dma->events[hc->event_dequeue];
    cache_invalidate(source, sizeof(*source));
    if ((source->control & TRB_CYCLE) !=
        (hc->event_cycle ? TRB_CYCLE : 0)) {
        return 0;
    }

    *event = *source;
    hc->event_count++;
    hc->event_dequeue++;
    if (hc->event_dequeue == XHCI_EVENT_TRBS) {
        hc->event_dequeue = 0;
        hc->event_cycle ^= 1;
    }
    mmio_write64(hc->runtime + 0x20 + 0x18,
                 dma_address(&hc->dma->events[hc->event_dequeue]) | (1u << 3));
    return 1;
}

static int wait_event(struct xhci_controller *hc, unsigned type,
                      uint64_t expected, struct xhci_trb *result) {
    for (unsigned elapsed = 0; elapsed < 1000; elapsed++) {
        struct xhci_trb event;
        while (next_event(hc, &event)) {
            unsigned event_type = TRB_TYPE_GET(event.control);
            if (event_type == TRB_TRANSFER_EVENT &&
                hc->keyboard_ready && event.parameter == hc->report_trb) {
                hc->report_pending = 0;
                cache_invalidate(hc->dma->report, sizeof(hc->dma->report));
                unsigned code = (event.status >> 24) & 0xffu;
                if (code == 1 || code == 13) {
                    if (!hc->first_report_logged) {
                        uart_puts("[usb] first keyboard report received\n");
                        hc->first_report_logged = 1;
                    }
                    hid_keyboard_process_report(&hc->keyboard,
                                                hc->dma->report);
                }
            }
            if (event_type == type &&
                (expected == 0 || event.parameter == expected)) {
                if (result != NULL) {
                    *result = event;
                }
                return (int)((event.status >> 24) & 0xffu);
            }
        }
        timer_delay_ms(1);
    }
    return -1;
}

static int command(struct xhci_controller *hc, uint64_t parameter,
                   uint32_t status, uint32_t control,
                   struct xhci_trb *completion) {
    uint64_t trb = ring_push(&hc->command, parameter, status, control);
    mmio_write32(hc->doorbells, 0);
    return wait_event(hc, TRB_COMMAND_EVENT, trb, completion);
}

static uint32_t *input_context(struct xhci_controller *hc, unsigned index) {
    return (uint32_t *)(void *)(hc->dma->input_context +
                                index * hc->context_size);
}

static int control_transfer(struct xhci_controller *hc, uint8_t request_type,
                            uint8_t request, uint16_t value, uint16_t index,
                            void *data, uint16_t length) {
    uint64_t setup = (uint64_t)request_type |
                     ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) |
                     ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);
    unsigned in = request_type & 0x80u;
    unsigned trt = length == 0 ? 0 : (in ? 3 : 2);

    ring_push(&hc->ep0, setup, 8,
              TRB_TYPE(TRB_SETUP) | TRB_IDT | (trt << 16));
    if (length != 0) {
        if (!in) {
            cache_clean(data, length);
        }
        ring_push(&hc->ep0, dma_address(data), length,
                  TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    }
    uint64_t status_trb =
        ring_push(&hc->ep0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC |
                  ((!length || !in) ? TRB_DIR_IN : 0));
    mmio_write32(hc->doorbells + hc->slot * 4u, 1);
    int completion =
        wait_event(hc, TRB_TRANSFER_EVENT, status_trb, NULL);
    if (completion == 1 && in && length != 0) {
        cache_invalidate(data, length);
    }
    return completion == 1 ? 0 : -1;
}

static int reset_port(struct xhci_controller *hc, unsigned port) {
    uint64_t reg = hc->op + 0x400 + (uint64_t)(port - 1) * 0x10;
    uint32_t status = mmio_read32(reg);
    if (!(status & PORTSC_CCS)) {
        return -1;
    }

    /*
     * PORTSC.PED is write-one-to-disable.  Never echo a sampled PED=1 back
     * while resetting or acknowledging change bits.
     */
    mmio_write32(reg, (status & ~(PORTSC_CHANGE_BITS | PORTSC_PED)) |
                          PORTSC_PP | PORTSC_PR);
    if (wait_bits(reg, PORTSC_PR, 0, 200) != 0 ||
        wait_bits(reg, PORTSC_PED, PORTSC_PED, 200) != 0) {
        return -1;
    }
    status = mmio_read32(reg);
    mmio_write32(reg, (status & ~PORTSC_PED) | PORTSC_CHANGE_BITS);
    hc->speed = (mmio_read32(reg) >> PORTSC_SPEED_SHIFT) & 0xfu;
    timer_delay_ms(100);
    return hc->speed == 0 ? -1 : 0;
}

static int address_device(struct xhci_controller *hc) {
    struct xhci_trb completion;
    int code = command(hc, 0, 0, TRB_TYPE(TRB_ENABLE_SLOT), &completion);
    if (code != 1) {
        return -1;
    }
    hc->slot = completion.control >> TRB_SLOT_SHIFT;
    if (hc->slot == 0 || hc->slot > XHCI_MAX_SLOTS) {
        return -1;
    }

    ring_init(&hc->ep0, hc->dma->ep0);
    zero_bytes(hc->dma->input_context, sizeof(hc->dma->input_context));
    zero_bytes(hc->dma->output_context, sizeof(hc->dma->output_context));

    uint32_t *control = input_context(hc, 0);
    uint32_t *slot = input_context(hc, 1);
    uint32_t *ep0 = input_context(hc, 2);
    control[1] = (1u << 0) | (1u << 1);
    slot[0] = (hc->speed << 20) | (1u << 27);
    slot[1] = hc->port << 16;
    unsigned packet = hc->speed == 4 ? 512 : (hc->speed == 3 ? 64 : 8);
    ep0[1] = (3u << 1) | (4u << 3) | (packet << 16);
    ep0[2] = (uint32_t)(dma_address(hc->dma->ep0) | 1u);
    ep0[3] = (uint32_t)(dma_address(hc->dma->ep0) >> 32);
    ep0[4] = 8;

    hc->dma->dcbaa[hc->slot] = dma_address(hc->dma->output_context);
    cache_clean(hc->dma->input_context, sizeof(hc->dma->input_context));
    cache_clean(hc->dma->output_context, sizeof(hc->dma->output_context));
    cache_clean(hc->dma->dcbaa, sizeof(hc->dma->dcbaa));

    code = command(hc, dma_address(hc->dma->input_context), 0,
                   TRB_TYPE(TRB_ADDRESS_DEVICE) |
                       (hc->slot << TRB_SLOT_SHIFT), NULL);
    return code == 1 ? 0 : -1;
}

static int update_ep0_packet_size(struct xhci_controller *hc,
                                  unsigned packet_size) {
    if (packet_size == 0) {
        return -1;
    }
    zero_bytes(hc->dma->input_context, sizeof(hc->dma->input_context));
    uint32_t *control = input_context(hc, 0);
    uint32_t *ep0 = input_context(hc, 2);
    control[1] = (1u << 1);
    ep0[1] = (3u << 1) | (4u << 3) | (packet_size << 16);
    cache_clean(hc->dma->input_context, sizeof(hc->dma->input_context));
    int code = command(hc, dma_address(hc->dma->input_context), 0,
                       TRB_TYPE(TRB_EVALUATE_CONTEXT) |
                           (hc->slot << TRB_SLOT_SHIFT), NULL);
    return code == 1 ? 0 : -1;
}

static int configure_keyboard(struct xhci_controller *hc) {
    uint8_t *buffer = hc->dma->descriptor;
    zero_bytes(buffer, sizeof(hc->dma->descriptor));

    if (control_transfer(hc, 0x80, USB_REQ_GET_DESCRIPTOR,
                         USB_DESC_DEVICE << 8, 0, buffer, 8) != 0) {
        return -1;
    }
    if (hc->speed <= 2 && buffer[7] != 8 &&
        update_ep0_packet_size(hc, buffer[7]) != 0) {
        return -1;
    }

    if (control_transfer(hc, 0x80, USB_REQ_GET_DESCRIPTOR,
                         USB_DESC_CONFIG << 8, 0, buffer, 9) != 0) {
        return -1;
    }
    unsigned total = (unsigned)buffer[2] | ((unsigned)buffer[3] << 8);
    if (total < 9 || total > sizeof(hc->dma->descriptor)) {
        return -1;
    }
    if (control_transfer(hc, 0x80, USB_REQ_GET_DESCRIPTOR,
                         USB_DESC_CONFIG << 8, 0, buffer,
                         (uint16_t)total) != 0) {
        return -1;
    }

    unsigned configuration = buffer[5];
    unsigned interface = 0;
    unsigned endpoint = 0;
    unsigned packet = 0;
    unsigned interval = 0;
    int keyboard_interface = 0;

    for (unsigned offset = 0; offset + 2 <= total;) {
        unsigned length = buffer[offset];
        unsigned type = buffer[offset + 1];
        if (length < 2 || offset + length > total) {
            return -1;
        }
        if (type == USB_DESC_INTERFACE && length >= 9) {
            keyboard_interface =
                buffer[offset + 5] == 3 && buffer[offset + 6] == 1 &&
                buffer[offset + 7] == 1;
            if (keyboard_interface) {
                interface = buffer[offset + 2];
            }
        } else if (type == USB_DESC_ENDPOINT && length >= 7 &&
                   keyboard_interface && (buffer[offset + 2] & 0x80) &&
                   (buffer[offset + 3] & 3) == 3) {
            endpoint = buffer[offset + 2] & 0x0f;
            packet = (unsigned)buffer[offset + 4] |
                     ((unsigned)buffer[offset + 5] << 8);
            interval = buffer[offset + 6];
            break;
        }
        offset += length;
    }
    if (endpoint == 0 || packet == 0 || packet > 64) {
        return -1;
    }

    if (control_transfer(hc, 0x00, USB_REQ_SET_CONFIG,
                         (uint16_t)configuration, 0, NULL, 0) != 0 ||
        control_transfer(hc, 0x21, USB_REQ_SET_PROTOCOL, 0,
                         (uint16_t)interface, NULL, 0) != 0) {
        return -1;
    }

    hc->interrupt_dci = endpoint * 2 + 1;
    hc->interrupt_packet = 8; /* HID boot reports are exactly eight bytes. */
    ring_init(&hc->interrupt, hc->dma->interrupt);
    zero_bytes(hc->dma->input_context, sizeof(hc->dma->input_context));
    uint32_t *control = input_context(hc, 0);
    uint32_t *slot = input_context(hc, 1);
    uint32_t *ep = input_context(hc, hc->interrupt_dci + 1);
    control[1] = (1u << 0) | (1u << hc->interrupt_dci);
    slot[0] = (hc->speed << 20) | (hc->interrupt_dci << 27);
    slot[1] = hc->port << 16;
    unsigned encoded_interval =
        hc->speed <= 2 ? interval + 2 : (interval ? interval - 1 : 0);
    if (encoded_interval > 15) {
        encoded_interval = 15;
    }
    ep[0] = encoded_interval << 16;
    ep[1] = (3u << 1) | (7u << 3) | (packet << 16);
    ep[2] = (uint32_t)(dma_address(hc->dma->interrupt) | 1u);
    ep[3] = (uint32_t)(dma_address(hc->dma->interrupt) >> 32);
    ep[4] = packet;
    cache_clean(hc->dma->input_context, sizeof(hc->dma->input_context));

    int code = command(hc, dma_address(hc->dma->input_context), 0,
                       TRB_TYPE(TRB_CONFIGURE_EP) |
                           (hc->slot << TRB_SLOT_SHIFT), NULL);
    if (code != 1) {
        return -1;
    }

    hid_keyboard_init(&hc->keyboard);
    hc->keyboard_ready = 1;
    return 0;
}

static void queue_report(struct xhci_controller *hc) {
    if (!hc->keyboard_ready || hc->report_pending) {
        return;
    }
    zero_bytes(hc->dma->report, sizeof(hc->dma->report));
    cache_clean(hc->dma->report, sizeof(hc->dma->report));
    hc->report_trb =
        ring_push(&hc->interrupt, dma_address(hc->dma->report),
                  hc->interrupt_packet, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    hc->report_pending = 1;
    mmio_write32(hc->doorbells + hc->slot * 4u, hc->interrupt_dci);
}

static int controller_init(struct xhci_controller *hc, uint64_t base,
                           struct xhci_dma_area *dma) {
    zero_bytes(hc, sizeof(*hc));
    zero_bytes(dma, sizeof(*dma));
    hc->base = base;
    hc->dma = dma;
    hc->stage = "capability-probe";

    unsigned cap_length = mmio_read8(base);
    uint16_t version = mmio_read16(base + 2);
    hc->capability_word0 = mmio_read32(base);
    hc->capability_length = (uint8_t)cap_length;
    hc->hci_version = version;
    uart_puts("[usb] probing xHCI at ");
    uart_puthex(base);
    uart_puts(" version=");
    uart_puthex(version);
    uart_puts("\n");
    if (cap_length < 0x20) {
        hc->stage = "invalid-caplength";
        uart_puts("[usb] invalid xHCI CAPLENGTH\n");
        return -1;
    }
    if (version < 0x0100 || version > 0x0120) {
        hc->stage = "invalid-hciversion";
        uart_puts("[usb] invalid xHCI HCIVERSION\n");
        return -1;
    }
    uint32_t hcs1 = mmio_read32(base + 4);
    uint32_t hcs2 = mmio_read32(base + 8);
    hc->hcsparams1 = hcs1;
    hc->hccparams1 = mmio_read32(base + 0x10);
    unsigned scratchpads =
        (((hcs2 >> 21) & 0x1fu) << 5) | ((hcs2 >> 27) & 0x1fu);
    if (scratchpads > XHCI_MAX_SCRATCHPADS) {
        hc->stage = "scratchpad-limit";
        uart_puts("[usb] too many xHCI scratchpads\n");
        return -1;
    }
    hc->max_ports = (hcs1 >> 24) & 0xffu;
    if (hc->max_ports > XHCI_MAX_PORTS) {
        hc->max_ports = XHCI_MAX_PORTS;
    }
    hc->context_size = (mmio_read32(base + 0x10) & (1u << 2)) ? 64 : 32;
    hc->op = base + cap_length;
    hc->doorbells = base + (mmio_read32(base + 0x14) & ~3u);
    hc->runtime = base + (mmio_read32(base + 0x18) & ~0x1fu);

    mmio_write32(hc->op, mmio_read32(hc->op) & ~USBCMD_RUN);
    if (wait_bits(hc->op + 4, USBSTS_HALTED, USBSTS_HALTED, 100) != 0) {
        hc->stage = "halt-timeout";
        uart_puts("[usb] xHCI failed to halt\n");
        return -1;
    }
    mmio_write32(hc->op, USBCMD_RESET);
    if (wait_bits(hc->op, USBCMD_RESET, 0, 100) != 0 ||
        wait_bits(hc->op + 4, USBSTS_CNR, 0, 100) != 0) {
        hc->stage = "reset-timeout";
        uart_puts("[usb] xHCI reset timed out\n");
        return -1;
    }

    ring_init(&hc->command, dma->command);
    hc->event_cycle = 1;
    for (unsigned i = 0; i < scratchpads; i++) {
        dma->scratchpad_array[i] = dma_address(dma->scratchpad_pages[i]);
    }
    if (scratchpads != 0) {
        dma->dcbaa[0] = dma_address(dma->scratchpad_array);
    }
    dma->erst.address = dma_address(dma->events);
    dma->erst.size = XHCI_EVENT_TRBS;
    cache_clean(dma, sizeof(*dma));

    mmio_write64(hc->op + 0x30, dma_address(dma->dcbaa));
    mmio_write64(hc->op + 0x18, dma_address(dma->command) | 1u);
    mmio_write32(hc->runtime + 0x20 + 0x08, 1);
    mmio_write64(hc->runtime + 0x20 + 0x10, dma_address(&dma->erst));
    mmio_write64(hc->runtime + 0x20 + 0x18, dma_address(dma->events));
    mmio_write32(hc->op + 0x38, XHCI_MAX_SLOTS);
    mmio_write32(hc->op, USBCMD_RUN);
    if (wait_bits(hc->op + 4, USBSTS_HALTED, 0, 100) != 0) {
        hc->stage = "run-timeout";
        uart_puts("[usb] xHCI failed to run\n");
        return -1;
    }
    hc->running = 1;
    hc->stage = "scanning-ports";

    for (unsigned port = 1; port <= hc->max_ports; port++) {
        uint64_t port_reg = hc->op + 0x400 + (uint64_t)(port - 1) * 0x10;
        uint32_t port_status = mmio_read32(port_reg);
        hc->last_portsc = port_status;
        hc->portsc[port - 1] = port_status;
        uart_puts("[usb] port ");
        uart_putuint(port);
        uart_puts(" PORTSC=");
        uart_puthex(port_status);
        uart_puts("\n");
        if (reset_port(hc, port) == 0) {
            hc->port = port;
            uart_puts("[usb] connected device speed=");
            uart_putuint(hc->speed);
            uart_puts("\n");
            if (address_device(hc) != 0) {
                hc->stage = "address-device-failed";
                uart_puts("[usb] Address Device failed\n");
                break;
            }
            if (configure_keyboard(hc) != 0) {
                hc->stage = "hid-config-failed";
                uart_puts("[usb] HID keyboard configuration failed\n");
                break;
            }
            queue_report(hc);
            hc->stage = "keyboard-running";
            return 0;
            /* This small host owns one slot per controller. */
        }
    }
    hc->stage = "no-keyboard";
    return 0;
}

static int enumerate_connected_keyboard(struct xhci_controller *hc) {
    int saw_connected = 0;
    for (unsigned port = 1; port <= hc->max_ports; port++) {
        uint64_t port_reg = hc->op + 0x400 + (uint64_t)(port - 1) * 0x10;
        uint32_t port_status = mmio_read32(port_reg);
        hc->portsc[port - 1] = port_status;
        if ((port_status & PORTSC_CCS) == 0) {
            continue;
        }
        saw_connected = 1;

        hc->stage = "hotplug-reset";
        if (reset_port(hc, port) != 0) {
            continue;
        }
        hc->port = port;
        hc->stage = "hotplug-address";
        if (address_device(hc) != 0) {
            hc->stage = "hotplug-address-failed";
            return -1;
        }
        hc->stage = "hotplug-hid-config";
        if (configure_keyboard(hc) != 0) {
            hc->stage = "hotplug-hid-config-failed";
            return -1;
        }

        queue_report(hc);
        hc->stage = "keyboard-running";
        uart_puts("[usb] hotplug boot keyboard ready controller=");
        uart_puthex(hc->base);
        uart_puts(" port=");
        uart_putuint(port);
        uart_puts("\n");
        return 1;
    }
    return saw_connected ? -1 : 0;
}

static void release_disconnected_keyboard(struct xhci_controller *hc) {
    unsigned old_slot = hc->slot;

    hc->keyboard_ready = 0;
    hc->report_pending = 0;
    hc->report_trb = 0;
    hc->slot = 0;
    hc->port = 0;
    hc->speed = 0;
    hc->interrupt_dci = 0;
    hc->interrupt_packet = 0;
    hc->enumeration_attempts = 0;
    hid_keyboard_init(&hc->keyboard);

    if (old_slot != 0) {
        hc->stage = "disconnect-disable-slot";
        if (command(hc, 0, 0,
                    TRB_TYPE(TRB_DISABLE_SLOT) |
                    (old_slot << TRB_SLOT_SHIFT), NULL) != 0) {
            hc->stage = "disconnect-disable-failed";
            return;
        }
    }
    hc->stage = "disconnected-scanning";
    uart_puts("[usb] keyboard disconnected; scanning resumed\n");
}

static void keyboard_poll(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < XHCI_CONTROLLER_COUNT; i++) {
        struct xhci_controller *hc = &controllers[i];
        if (!hc->running) {
            continue;
        }
        hc->poll_count++;
        hc->last_usbsts = mmio_read32(hc->op + 4);
        struct xhci_trb event;
        while (next_event(hc, &event)) {
            if (TRB_TYPE_GET(event.control) == TRB_TRANSFER_EVENT &&
                hc->keyboard_ready && event.parameter == hc->report_trb) {
                hc->report_pending = 0;
                cache_invalidate(hc->dma->report, sizeof(hc->dma->report));
                unsigned code = (event.status >> 24) & 0xffu;
                if (code == 1 || code == 13) {
                    hc->report_count++;
                    if (!hc->first_report_logged) {
                        uart_puts("[usb] first keyboard report received\n");
                        hc->first_report_logged = 1;
                    }
                    hid_keyboard_process_report(&hc->keyboard,
                                                hc->dma->report);
                }
            }
        }
        uint8_t connected_mask = 0;
        for (unsigned port = 1; port <= hc->max_ports; port++) {
            uint64_t port_reg =
                hc->op + 0x400 + (uint64_t)(port - 1) * 0x10;
            hc->portsc[port - 1] = mmio_read32(port_reg);
            if ((hc->portsc[port - 1] & PORTSC_CCS) != 0) {
                connected_mask |= (uint8_t)(1u << (port - 1));
            }
        }
        if ((connected_mask & ~hc->connected_mask) != 0) {
            hc->enumeration_attempts = 0;
        }
        hc->connected_mask = connected_mask;

        if (hc->keyboard_ready &&
            (hc->port == 0 ||
             (connected_mask & (1u << (hc->port - 1))) == 0)) {
            release_disconnected_keyboard(hc);
        }
        /*
         * A controller reset temporarily disconnects some keyboards.  The
         * device may not reappear until after the one-time boot scan, so
         * retry enumeration from the polling path once the connect bit is
         * observed.  Bound failed attempts to avoid a serial/log storm from
         * an unsupported device.
         */
        if (!hc->keyboard_ready && (hc->poll_count % 125u) == 0 &&
            hc->enumeration_attempts < 3u) {
            int result = enumerate_connected_keyboard(hc);
            if (result != 0) {
                hc->enumeration_attempts++;
            }
        }
        queue_report(hc);
    }
    timer_schedule_interrupt_ms(8, keyboard_poll, NULL);
}

int xhci_keyboard_init(void) {
    uint64_t pci[16];
    uint64_t bases[XHCI_CONTROLLER_COUNT];
    uart_rpi_get_pci_debug(pci);

    /*
     * RP1 xHCI0/xHCI1 are at offsets 2 MiB and 3 MiB in the large AXI BAR.
     * Derive its CPU address from the firmware's live PCIe configuration
     * instead of assuming the BAR layout installed by another boot stack.
     */
    if (pci[8] == 0) {
        uart_puts("[usb] RP1 BAR1 has no CPU mapping\n");
        return 0;
    }
    bases[0] = pci[8] + UINT64_C(0x00200000);
    bases[1] = pci[8] + UINT64_C(0x00300000);

    uart_puts("[usb] firmware xHCI bases ");
    uart_puthex(bases[0]);
    uart_puts(" ");
    uart_puthex(bases[1]);
    uart_puts("\n");

    int started = 0;
    for (unsigned i = 0; i < XHCI_CONTROLLER_COUNT; i++) {
        if (controller_init(&controllers[i], bases[i], &dma_areas[i]) == 0) {
            started++;
            uart_puts(controllers[i].keyboard_ready
                          ? "[usb] boot keyboard ready\n"
                          : "[usb] xHCI ready (no direct keyboard)\n");
        } else {
            uart_puts("[usb] xHCI controller init failed\n");
        }
    }
    if (started != 0) {
        timer_schedule_interrupt_ms(8, keyboard_poll, NULL);
    }
    return started;
}

int xhci_keyboard_format_status(char *buffer, unsigned long size) {
    if (buffer == NULL || size == 0) {
        return -1;
    }
    uint64_t received = 0;
    uint64_t read = 0;
    uint64_t pci[16];
    usb_keyboard_get_counters(&received, &read);
    uart_rpi_get_pci_debug(pci);
    int length = snprintf(buffer, size,
                          "build: rpi5-rp1-xhci-hid\n"
                          "devnode: /dev/usb0\n"
                          "usb_bytes_received: %u\n"
                          "usb_bytes_read: %u\n"
                          "pcie_link_status: 0x%x\n"
                          "rp1_pci_id: 0x%x\n"
                          "rp1_pci_command: 0x%x\n"
                          "rp1_bar0: 0x%x\n"
                          "rp1_bar1: 0x%x\n"
                          "rp1_bar2: 0x%x\n"
                          "rp1_bar3: 0x%x\n"
                          "rp1_bar0_cpu: 0x%lx\n"
                          "rp1_bar1_cpu: 0x%lx\n"
                          "rp1_bar2_cpu: 0x%lx\n",
                          (unsigned)received, (unsigned)read,
                          (unsigned)pci[0], (unsigned)pci[1],
                          (unsigned)pci[2], (unsigned)pci[3],
                          (unsigned)pci[4], (unsigned)pci[5],
                          (unsigned)pci[6], (unsigned long)pci[7],
                          (unsigned long)pci[8], (unsigned long)pci[9]);
    if ((unsigned long)length < size) {
        int added = snprintf(buffer + length, size - (unsigned long)length,
                             "rc_bar1_lo: 0x%x\n"
                             "rc_bar1_hi: 0x%x\n"
                             "rc_bar2_lo: 0x%x\n"
                             "rc_bar2_hi: 0x%x\n"
                             "rc_bar2_remap_lo: 0x%x\n"
                             "rc_bar2_remap_hi: 0x%x\n",
                             (unsigned)pci[10], (unsigned)pci[11],
                             (unsigned)pci[12], (unsigned)pci[13],
                             (unsigned)pci[14], (unsigned)pci[15]);
        if (added < 0) {
            return added;
        }
        length += added;
    }
    for (unsigned i = 0; i < XHCI_CONTROLLER_COUNT; i++) {
        struct xhci_controller *hc = &controllers[i];
        unsigned long used = length > 0 ? (unsigned long)length : 0;
        if (used >= size) {
            break;
        }
        int added = snprintf(buffer + used, size - used,
                             "\ncontroller: %u\n"
                             "base: 0x%lx\n"
                             "stage: %s\n"
                             "capability_word0: 0x%x\n"
                             "caplength: 0x%x\n"
                             "hciversion: 0x%x\n"
                             "hcsparams1: 0x%x\n"
                             "hccparams1: 0x%x\n"
                             "running: %u\n"
                             "keyboard_ready: %u\n"
                             "port: %u\n"
                             "speed: %u\n"
                             "usbsts: 0x%x\n"
                             "portsc: 0x%x\n"
                             "events: %u\n"
                             "reports: %u\n"
                             "report_pending: %u\n",
                             i, (unsigned long)hc->base,
                             hc->stage != NULL ? hc->stage : "not-attempted",
                             hc->capability_word0, hc->capability_length,
                             hc->hci_version, hc->hcsparams1, hc->hccparams1,
                             hc->running, hc->keyboard_ready, hc->port,
                             hc->speed, hc->last_usbsts, hc->last_portsc,
                             hc->event_count, hc->report_count,
                             hc->report_pending);
        if (added < 0) {
            return added;
        }
        length += added;

        used = length > 0 ? (unsigned long)length : 0;
        if (used < size) {
            added = snprintf(buffer + used, size - used,
                             "command_ring_bus: 0x%lx\n"
                             "event_ring_bus: 0x%lx\n"
                             "crcr: 0x%lx\n"
                             "erstba: 0x%lx\n"
                             "erdp: 0x%lx\n",
                             (unsigned long)dma_address(hc->dma->command),
                             (unsigned long)dma_address(hc->dma->events),
                             (unsigned long)(
                                 mmio_read32(hc->op + 0x18) |
                                 ((uint64_t)mmio_read32(hc->op + 0x1c) << 32)),
                             (unsigned long)(
                                 mmio_read32(hc->runtime + 0x30) |
                                 ((uint64_t)mmio_read32(hc->runtime + 0x34) << 32)),
                             (unsigned long)(
                                 mmio_read32(hc->runtime + 0x38) |
                                 ((uint64_t)mmio_read32(hc->runtime + 0x3c) << 32)));
            if (added < 0) {
                return added;
            }
            length += added;
        }

        for (unsigned port = 0; port < hc->max_ports; port++) {
            used = length > 0 ? (unsigned long)length : 0;
            if (used >= size) {
                break;
            }
            added = snprintf(buffer + used, size - used,
                             "port%u_portsc: 0x%x\n",
                             port + 1, hc->portsc[port]);
            if (added < 0) {
                return added;
            }
            length += added;
        }
    }
    return length;
}

#else

int xhci_keyboard_init(void) {
    return 0;
}

int xhci_keyboard_format_status(char *buffer, unsigned long size) {
    return snprintf(buffer, size,
                    "build: qemu\nusb: disabled on this platform\n");
}

#endif
