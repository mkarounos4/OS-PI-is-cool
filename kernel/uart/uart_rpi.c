#include "rpi5_addresses.h"
#include "uart.h"
#include "uart_device.h"

#include <stddef.h>

#include "irq/irq.h"

#define UART_DR     0x00
#define UART_FR     0x18
#define UART_IBRD   0x24
#define UART_FBRD   0x28
#define UART_LCRH   0x2c
#define UART_CR     0x30
#define UART_IFLS   0x34
#define UART_IMSC   0x38
#define UART_RIS    0x3c
#define UART_MIS    0x40
#define UART_ICR    0x44

#define FR_TXFF     (1u << 5)
#define FR_RXFE     (1u << 4)
#define FR_BUSY     (1u << 3)

#define LCRH_WLEN_8 (3u << 5)
#define LCRH_FEN    (1u << 4)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

#define UART_INT_RX     (1u << 4)
#define UART_INT_TX     (1u << 5)
#define UART_INT_RT     (1u << 6)
#define UART_INT_FE     (1u << 7)
#define UART_INT_PE     (1u << 8)
#define UART_INT_BE     (1u << 9)
#define UART_INT_OE     (1u << 10)
#define UART_INT_RX_ALL (UART_INT_RX | UART_INT_RT | UART_INT_FE | UART_INT_PE | UART_INT_BE | UART_INT_OE)

#define UART_IFLS_RX_1_8 0u
#define RP1_INT_UART0    25u
#define UART_RX_BUFFER_SIZE 4096u

#define GIC_SPI_INTID(spi)          (32u + (spi))
#define RPI5_MIP0_GIC_SPI_BASE      128u
#define RP1_UART0_CPU_INTID         (GIC_SPI_INTID(RPI5_MIP0_GIC_SPI_BASE) + RP1_INT_UART0)

#define PCIE_EXT_CFG_INDEX          0x9000u
#define PCIE_EXT_CFG_DATA           0x8000u
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO          0x400cu
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI          0x4010u
#define PCIE_MISC_RC_BAR1_CONFIG_LO 0x402cu
#define PCIE_MISC_UBUS_BAR1_REMAP   0x40acu
#define PCIE_MISC_PCIE_STATUS       0x4068u
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT  0x4070u
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI     0x4080u
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI    0x4084u
#define PCIE_STATUS_DL_ACTIVE       (1u << 5)
#define PCIE_STATUS_PHYLINKUP       (1u << 4)
#define PCIE_UBUS_REMAP_ACCESS_EN   (1u << 0)
#define PCIE_OUTBOUND_WIN_LO(win)   (PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO + ((win) * 8u))
#define PCIE_OUTBOUND_WIN_HI(win)   (PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI + ((win) * 8u))
#define PCIE_OUTBOUND_BASE_LIMIT(win) \
                                    (PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT + ((win) * 4u))
#define PCIE_OUTBOUND_BASE_HI(win)  (PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI + ((win) * 8u))
#define PCIE_OUTBOUND_LIMIT_HI(win) (PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI + ((win) * 8u))
#define PCIE_INBOUND_BAR_LO(bar)    (PCIE_MISC_RC_BAR1_CONFIG_LO + (((bar) - 1u) * 8u))
#define PCIE_INBOUND_UBUS(bar)      (PCIE_MISC_UBUS_BAR1_REMAP + (((bar) - 1u) * 8u))
#define PCIE_OUTBOUND_WINDOWS       4u

#define PCI_COMMAND_STATUS          0x04u
#define PCI_BAR0                    0x10u
#define PCI_PRIMARY_BUS             0x18u
#define PCI_CAP_PTR                 0x34u
#define PCI_STATUS_CAP_LIST         (1u << 20)
#define PCI_COMMAND_MEM_SPACE       (1u << 1)
#define PCI_COMMAND_BUS_MASTER      (1u << 2)
#define PCI_VENDOR_ID_RPI           0x1de4u
#define PCI_DEVICE_ID_RP1_C0        0x0001u
#define PCI_CAP_ID_MSIX             0x11u
#define PCI_MSIX_CTRL_ENABLE        (1u << 31)
#define PCI_MSIX_CTRL_FUNCTION_MASK (1u << 30)
#define PCI_MSIX_TABLE_BIR_MASK     0x7u
#define PCI_MSIX_TABLE_OFFSET_MASK  (~0x7u)
#define PCI_MSIX_ENTRY_SIZE         16u
#define PCI_MSIX_VECTOR_MASKED      (1u << 0)
#define PCI_BAR_IO_SPACE            (1u << 0)
#define PCI_BAR_MEM_TYPE_MASK       0x6u
#define PCI_BAR_MEM_TYPE_64         0x4u
#define PCI_BAR_MEM_ADDR_MASK       (~0xfu)
#define RP1_PCI_SECONDARY_BUS       1u
#define RP1_BAR0_SIZE               UINT64_C(0x00400000)
#define RP1_BAR1_SIZE               UINT64_C(0x00410000)
#define PCIE2_NP_PCI_BASE           UINT64_C(0x0000000000)
#define PCIE2_NP_CPU_BASE           RPI5_RP1_BAR0_BASE
#define PCIE2_NP_SIZE               UINT64_C(0x0100000000)
#define PCIE2_PREF_PCI_BASE         UINT64_C(0x0400000000)
#define PCIE2_PREF_CPU_BASE         RPI5_RP1_PERIPH_BASE
#define PCIE2_PREF_SIZE             UINT64_C(0x0300000000)
#define PCIE2_RP1_NP_OUTBOUND_WIN   0u
#define MIP0_MSIX_MSG_ADDR          UINT64_C(0x000000fffffff000)
#define MIP0_MSI_INBOUND_BAR        3u
#define MIP0_MSI_INBOUND_SIZE_4K    0x1cu

#define MIP_INT_CLEAR               0x10u
#define MIP_INT_CFGL_HOST           0x20u
#define MIP_INT_CFGH_HOST           0x30u
#define MIP_INT_MASKL_HOST          0x40u
#define MIP_INT_MASKH_HOST          0x50u
#define MIP_INT_MASKL_VPU           0x60u
#define MIP_INT_MASKH_VPU           0x70u
#define MIP_INT_STATUSL_HOST        0x80u

#define RP1_PCIE_REG_RW             0x000u
#define RP1_PCIE_REG_SET            0x800u
#define RP1_MSIX_CFG(hwirq)         (0x8u + ((hwirq) * 4u))
#define RP1_MSIX_CFG_ENABLE         (1u << 0)
#define RP1_MSIX_CFG_IACK           (1u << 2)
#define RP1_MSIX_CFG_IACK_EN        (1u << 3)

static unsigned char uart_rx_buffer[UART_RX_BUFFER_SIZE];

static void uart_rx_buffer_clear(void) {
    for (size_t i = 0; i < UART_RX_BUFFER_SIZE; i++) {
        uart_rx_buffer[i] = 0;
    }
}

#ifndef RP1_UARTCLK_HZ
#define RP1_UARTCLK_HZ 50000000u
#endif

#define UART_BAUD        115200u
#define UART_DIV_X64     (((RP1_UARTCLK_HZ * 4u) + (UART_BAUD / 2u)) / UART_BAUD)
#define UART_IBRD_VALUE  (UART_DIV_X64 / 64u)
#define UART_FBRD_VALUE  (UART_DIV_X64 % 64u)
#define UART_POLL_LIMIT  1000000u

static void gpio_set_function(unsigned pin, uint32_t function) {
    rpi5_mmio_write32(RPI5_GPIO_CTRL(pin), function);
}

static void gpio_set_pad(unsigned pin, uint32_t pad_value) {
    rpi5_mmio_write32(RPI5_GPIO_PAD(pin), pad_value);
}

static int pcie2_link_up(void) {
    uint32_t status = rpi5_mmio_read32(RPI5_PCIE2_BASE + PCIE_MISC_PCIE_STATUS);
    uint32_t required = PCIE_STATUS_DL_ACTIVE | PCIE_STATUS_PHYLINKUP;

    return (status & required) == required;
}

static uint32_t pcie2_root_read32(unsigned where) {
    return rpi5_mmio_read32(RPI5_PCIE2_BASE + (where & ~3u));
}

static void pcie2_root_write32(unsigned where, uint32_t value) {
    rpi5_mmio_write32(RPI5_PCIE2_BASE + (where & ~3u), value);
    rpi5_mmio_barrier();
}

static uint32_t pcie2_root_reg32(unsigned where) {
    return rpi5_mmio_read32(RPI5_PCIE2_BASE + (where & ~3u));
}

static void pcie2_set_mip0_msi_window(void) {
    unsigned bar = MIP0_MSI_INBOUND_BAR;

    pcie2_root_write32(PCIE_INBOUND_BAR_LO(bar),
                       (uint32_t)MIP0_MSIX_MSG_ADDR |
                       MIP0_MSI_INBOUND_SIZE_4K);
    pcie2_root_write32(PCIE_INBOUND_BAR_LO(bar) + 4u,
                       (uint32_t)(MIP0_MSIX_MSG_ADDR >> 32));
    pcie2_root_write32(PCIE_INBOUND_UBUS(bar),
                       ((uint32_t)RPI5_MIP0_BASE & ~0xfffu) |
                       PCIE_UBUS_REMAP_ACCESS_EN);
    pcie2_root_write32(PCIE_INBOUND_UBUS(bar) + 4u,
                       (uint32_t)(RPI5_MIP0_BASE >> 32));
    rpi5_mmio_barrier();
}

static uint32_t pcie2_cfg_read32(unsigned bus, unsigned devfn, unsigned where) {
    uint32_t index = (bus << 20) | (devfn << 12);

    rpi5_mmio_write32(RPI5_PCIE2_BASE + PCIE_EXT_CFG_INDEX, index);
    rpi5_mmio_barrier();
    return rpi5_mmio_read32(RPI5_PCIE2_BASE + PCIE_EXT_CFG_DATA + (where & ~3u));
}

static void pcie2_cfg_write32(unsigned bus, unsigned devfn, unsigned where,
                              uint32_t value) {
    uint32_t index = (bus << 20) | (devfn << 12);

    rpi5_mmio_write32(RPI5_PCIE2_BASE + PCIE_EXT_CFG_INDEX, index);
    rpi5_mmio_barrier();
    rpi5_mmio_write32(RPI5_PCIE2_BASE + PCIE_EXT_CFG_DATA + (where & ~3u), value);
    rpi5_mmio_barrier();
}

static uint8_t pcie2_cfg_read8(unsigned bus, unsigned devfn, unsigned where) {
    uint32_t value = pcie2_cfg_read32(bus, devfn, where);
    return (uint8_t)((value >> ((where & 3u) * 8u)) & 0xffu);
}

static int rp1_find_pci_device(unsigned *bus_out, unsigned *devfn_out) {
    if (!pcie2_link_up()) {
        uart_puts("[uart] PCIe2 link down status=");
        uart_puthex(rpi5_mmio_read32(RPI5_PCIE2_BASE + PCIE_MISC_PCIE_STATUS));
        uart_puts("\n");
        return -1;
    }

    uint32_t bus_regs = pcie2_root_read32(PCI_PRIMARY_BUS);
    unsigned secondary = (bus_regs >> 8) & 0xffu;
    unsigned subordinate = (bus_regs >> 16) & 0xffu;

    if (secondary == 0 || subordinate < secondary) {
        secondary = RP1_PCI_SECONDARY_BUS;
        subordinate = RP1_PCI_SECONDARY_BUS;
        pcie2_root_write32(PCI_PRIMARY_BUS,
                           (bus_regs & 0xff000000u) |
                           (secondary << 8) |
                           (subordinate << 16));
    }

    if (subordinate > secondary + 7u) {
        subordinate = secondary + 7u;
    }

    uint32_t first_id = 0;
    for (unsigned bus = secondary; bus <= subordinate; bus++) {
        uint32_t id = pcie2_cfg_read32(bus, 0, 0);
        if (bus == secondary) {
            first_id = id;
        }

        if ((id & 0xffffu) == PCI_VENDOR_ID_RPI &&
            ((id >> 16) & 0xffffu) == PCI_DEVICE_ID_RP1_C0) {
            *bus_out = bus;
            *devfn_out = 0;
            return 0;
        }
    }

    uart_puts("[uart] RP1 PCI scan miss bus=");
    uart_puthex(secondary);
    uart_puts("-");
    uart_puthex(subordinate);
    uart_puts(" id0=");
    uart_puthex(first_id);
    uart_puts("\n");

    return -1;
}

static unsigned rp1_find_msix_cap(unsigned bus, unsigned devfn) {
    uint32_t command_status = pcie2_cfg_read32(bus, devfn, PCI_COMMAND_STATUS);
    uint8_t cap = pcie2_cfg_read8(bus, devfn, PCI_CAP_PTR) & ~3u;

    if ((command_status & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }

    for (unsigned guard = 0; cap >= 0x40u && guard < 48u; guard++) {
        uint8_t id = pcie2_cfg_read8(bus, devfn, cap);
        uint8_t next = pcie2_cfg_read8(bus, devfn, cap + 1u) & ~3u;

        if (id == PCI_CAP_ID_MSIX) {
            return cap;
        }

        cap = next;
    }

    return 0;
}

static int pcie2_outbound_pci_to_cpu_addr(uint64_t pci_addr, uint64_t *cpu_addr) {
    for (unsigned win = 0; win < PCIE_OUTBOUND_WINDOWS; win++) {
        uint64_t pcie_base = pcie2_root_reg32(PCIE_OUTBOUND_WIN_LO(win)) |
                             ((uint64_t)pcie2_root_reg32(PCIE_OUTBOUND_WIN_HI(win)) << 32);
        uint32_t base_limit = pcie2_root_reg32(PCIE_OUTBOUND_BASE_LIMIT(win));
        uint64_t cpu_base_mb = ((base_limit >> 4) & 0xfffu) |
                               (((uint64_t)pcie2_root_reg32(PCIE_OUTBOUND_BASE_HI(win)) & 0xffu) << 12);
        uint64_t cpu_limit_mb = ((base_limit >> 20) & 0xfffu) |
                                (((uint64_t)pcie2_root_reg32(PCIE_OUTBOUND_LIMIT_HI(win)) & 0xffu) << 12);

        if (cpu_limit_mb < cpu_base_mb) {
            continue;
        }

        uint64_t size = ((cpu_limit_mb - cpu_base_mb) + 1u) << 20;
        if (size == 0) {
            continue;
        }

        if (pci_addr >= pcie_base && pci_addr < pcie_base + size) {
            *cpu_addr = (cpu_base_mb << 20) + (pci_addr - pcie_base);
            return 0;
        }
    }

    return -1;
}

static int pcie2_pci_to_cpu_addr(uint64_t pci_addr, uint64_t *cpu_addr) {
    if (pcie2_outbound_pci_to_cpu_addr(pci_addr, cpu_addr) == 0) {
        return 0;
    }

    if (pci_addr < PCIE2_NP_SIZE) {
        *cpu_addr = PCIE2_NP_CPU_BASE + (pci_addr - PCIE2_NP_PCI_BASE);
        return 0;
    }

    if (pci_addr >= PCIE2_PREF_PCI_BASE &&
        pci_addr < PCIE2_PREF_PCI_BASE + PCIE2_PREF_SIZE) {
        *cpu_addr = PCIE2_PREF_CPU_BASE + (pci_addr - PCIE2_PREF_PCI_BASE);
        return 0;
    }

    return -1;
}

static int rp1_bar_cpu_base(unsigned bus, unsigned devfn, unsigned bir,
                            uint64_t *base) {
    uint32_t low = pcie2_cfg_read32(bus, devfn, PCI_BAR0 + (bir * 4u));
    uint64_t pci_addr;

    if ((low & PCI_BAR_IO_SPACE) != 0) {
        return -1;
    }

    pci_addr = (uint64_t)(low & PCI_BAR_MEM_ADDR_MASK);
    if ((low & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64) {
        if (bir >= 5u) {
            return -1;
        }
        pci_addr |= (uint64_t)pcie2_cfg_read32(bus, devfn,
                                               PCI_BAR0 + ((bir + 1u) * 4u))
                    << 32;
    }

    return pcie2_pci_to_cpu_addr(pci_addr, base);
}

static uint64_t rp1_msix_table_base(unsigned bus, unsigned devfn,
                                    uint32_t table_info) {
    unsigned bir = table_info & PCI_MSIX_TABLE_BIR_MASK;
    uint32_t offset = table_info & PCI_MSIX_TABLE_OFFSET_MASK;
    uint64_t bar_base = 0;

    if ((bir == 0u && offset < RP1_BAR0_SIZE) ||
        (bir == 1u && offset < RP1_BAR1_SIZE)) {
        if (rp1_bar_cpu_base(bus, devfn, bir, &bar_base) == 0) {
            return bar_base + offset;
        }
    }

    uart_puts("[uart] unsupported RP1 MSI-X table bir=");
    uart_puthex(bir);
    uart_puts(" offset=");
    uart_puthex(offset);
    uart_puts(" bar=");
    uart_puthex(bar_base);
    uart_puts("\n");
    return 0;
}

static int rp1_configure_msix_table(void) {
    unsigned bus = 0;
    unsigned devfn = 0;

    if (rp1_find_pci_device(&bus, &devfn) != 0) {
        uart_puts("[uart] RP1 PCI device not found\n");
        return -1;
    }

    uint32_t command_status = pcie2_cfg_read32(bus, devfn, PCI_COMMAND_STATUS);
    pcie2_cfg_write32(bus, devfn, PCI_COMMAND_STATUS,
                      (command_status & 0xffffu) |
                      PCI_COMMAND_MEM_SPACE | PCI_COMMAND_BUS_MASTER);

    unsigned msix_cap = rp1_find_msix_cap(bus, devfn);
    if (msix_cap == 0) {
        uart_puts("[uart] RP1 MSI-X capability not found\n");
        return -1;
    }

    uint32_t msix_ctrl = pcie2_cfg_read32(bus, devfn, msix_cap);
    uint32_t old_msix_ctrl = msix_ctrl;
    pcie2_cfg_write32(bus, devfn, msix_cap,
                      msix_ctrl | PCI_MSIX_CTRL_ENABLE |
                      PCI_MSIX_CTRL_FUNCTION_MASK);

    uint64_t table_base =
        rp1_msix_table_base(bus, devfn,
                            pcie2_cfg_read32(bus, devfn, msix_cap + 4u));
    if (table_base == 0) {
        pcie2_cfg_write32(bus, devfn, msix_cap, old_msix_ctrl);
        return -1;
    }

    uint64_t entry = table_base + ((uint64_t)RP1_INT_UART0 * PCI_MSIX_ENTRY_SIZE);
    rpi5_mmio_write32(entry + 12u, PCI_MSIX_VECTOR_MASKED);
    rpi5_mmio_barrier();
    rpi5_mmio_write32(entry + 0u, (uint32_t)MIP0_MSIX_MSG_ADDR);
    rpi5_mmio_write32(entry + 4u, (uint32_t)(MIP0_MSIX_MSG_ADDR >> 32));
    rpi5_mmio_write32(entry + 8u, RP1_INT_UART0);
    rpi5_mmio_barrier();
    rpi5_mmio_write32(entry + 12u, 0);
    rpi5_mmio_barrier();

    msix_ctrl = pcie2_cfg_read32(bus, devfn, msix_cap);
    msix_ctrl |= PCI_MSIX_CTRL_ENABLE;
    msix_ctrl &= ~PCI_MSIX_CTRL_FUNCTION_MASK;
    pcie2_cfg_write32(bus, devfn, msix_cap, msix_ctrl);

    return 0;
}

static void mip0_enable_uart0_vector(void) {
    uint32_t bit = 1u << RP1_INT_UART0;

    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_MASKL_HOST, 0);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_MASKH_HOST, 0);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_MASKL_VPU, ~0u);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_MASKH_VPU, ~0u);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_CFGL_HOST, ~0u);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_CFGH_HOST, ~0u);
    rpi5_mmio_write32(RPI5_MIP0_BASE + MIP_INT_CLEAR, bit);
    rpi5_mmio_barrier();
}

static void rp1_msix_cfg_set(unsigned hwirq, uint32_t value) {
    rpi5_mmio_write32(RPI5_RP1_PCIE_APBS_BASE + RP1_PCIE_REG_SET +
                      RP1_MSIX_CFG(hwirq), value);
}

static void rp1_uart0_msix_enable(void) {
    rp1_msix_cfg_set(RP1_INT_UART0, RP1_MSIX_CFG_IACK_EN);
    rp1_msix_cfg_set(RP1_INT_UART0, RP1_MSIX_CFG_IACK);
    rp1_msix_cfg_set(RP1_INT_UART0, RP1_MSIX_CFG_ENABLE);
    rpi5_mmio_barrier();
}

static void rp1_uart0_msix_iack(void) {
    rp1_msix_cfg_set(RP1_INT_UART0, RP1_MSIX_CFG_IACK);
    rpi5_mmio_barrier();
}

static void rp1_uart0_irq_bridge_init(void) {
    pcie2_set_mip0_msi_window();

    if (rp1_configure_msix_table() != 0) {
        uart_puts("[uart] continuing with existing RP1 MSI-X table\n");
    }

    mip0_enable_uart0_vector();
    rp1_uart0_msix_enable();
}

static struct trap_frame *uart_irq_handler(unsigned intid, struct trap_frame *frame, void *ctx) {
    (void)intid;
    (void)ctx;

    uint32_t status = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_MIS);
    if ((status & UART_INT_RX_ALL) == 0) {
        rp1_uart0_msix_iack();
        return frame;
    }

    uart_rx_interrupts_disable();
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_ICR, status & UART_INT_RX_ALL);
    uart_rx_interrupt_hook();
    rp1_uart0_msix_iack();
    return frame;
}

uint64_t get_uart_base(void) {
    return RPI5_RP1_UART0_BASE;
}

void uart_init(void) {
    // GPIO14/15 are pins 8/10 on the Pi 5 40-pin header.
    gpio_set_function(14, RPI5_GPIO_FUNC_UART0);
    gpio_set_function(15, RPI5_GPIO_FUNC_UART0);
    gpio_set_pad(14, RPI5_PAD_IE | RPI5_PAD_DRIVE_8MA | RPI5_PAD_SCHMITT);
    gpio_set_pad(15, RPI5_PAD_IE | RPI5_PAD_DRIVE_8MA | RPI5_PAD_PUE | RPI5_PAD_SCHMITT);

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_CR, 0);

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_LCRH, 0);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_ICR, 0x7ffu);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, 0);

    // IBRD/FBRD must be written before LCRH; the LCRH write latches them.
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IBRD, UART_IBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_FBRD, UART_FBRD_VALUE);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IFLS, UART_IFLS_RX_1_8);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_LCRH, LCRH_WLEN_8 | LCRH_FEN);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
    rpi5_mmio_barrier();
}

void uart_irq_init(void) {
    if (irq_register(RP1_UART0_CPU_INTID, uart_irq_handler, NULL) == 0) {
        irq_set_edge_triggered(RP1_UART0_CPU_INTID);
        irq_enable_line(RP1_UART0_CPU_INTID);
        rp1_uart0_irq_bridge_init();
        uart_rx_interrupts_enable();
        uart_puts("[uart] RX IRQ armed intid=");
        uart_puthex(RP1_UART0_CPU_INTID);
        uart_puts("\n");
    } else {
        uart_puts("[uart] RX IRQ register failed\n");
    }
}

void uart_rx_interrupts_enable(void) {
    uint32_t mask = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, mask | UART_INT_RX_ALL);
    rpi5_mmio_barrier();
}

void uart_rx_interrupts_disable(void) {
    uint32_t mask = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_IMSC);
    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_IMSC, mask & ~UART_INT_RX_ALL);
    rpi5_mmio_barrier();
}

void uart_rx_interrupt_hook(void) {
    size_t size = 0;

    while ((rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_FR) & FR_RXFE) == 0) {
        uint32_t data = rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_DR);
        if (size < UART_RX_BUFFER_SIZE) {
            unsigned char next = (unsigned char)(data & 0xffu);
            uart_rx_buffer[size++] = next;
        }
    }

    if (size > 0) {
        uart_char_device_receive((const char *)uart_rx_buffer, size);
    }

    uart_rx_buffer_clear();
    uart_rx_interrupts_enable();
}

void uart_raw_putc(const char c) {
    uint32_t timeout = UART_POLL_LIMIT;

    while ((rpi5_mmio_read32(RPI5_RP1_UART0_BASE + UART_FR) & FR_TXFF) && timeout) {
        timeout--;
    }

    rpi5_mmio_write32(RPI5_RP1_UART0_BASE + UART_DR, (uint32_t)(uint8_t)c);
}

void uart_putc(const char c) {
    if (c == '\n') {
        uart_raw_putc('\r');
    }
    uart_raw_putc(c);
}

void uart_puts(const char *s) {
    uart_raw_puts(s);
}

void uart_raw_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_raw_putc('\r');
        }
        uart_raw_putc(*s++);
    }
}

void uart_putuint(unsigned int u)
{
    if (u >= 10)
        uart_putuint(u / 10);

    uart_putc('0' + (u % 10));
}

void uart_putint(int i)
{
    if (i < 0) {
        uart_putc('-');
        uart_putuint((unsigned int)(-i));
    } else {
        uart_putuint((unsigned int)i);
    }
}

void uart_puthex(uint64_t value) {
    uart_raw_putc('0');
    uart_raw_putc('x');

    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xf;
        char c = nibble < 10 ? (char)('0' + nibble) : (char)('A' + (nibble - 10));

        if (started || c != '0' || i == 0) {
            started = 1;
            uart_raw_putc(c);
        }
    }
}
