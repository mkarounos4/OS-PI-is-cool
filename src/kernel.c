// =============================================================
//  kernel.c  —  Bare-metal kernel for Raspberry Pi 5
//
//  GPIO Architecture Note
//  ----------------------
//  On the Pi 5, ALL GPIO signals are routed through the RP1
//  I/O companion chip (connected to the BCM2712 via PCIe).
//  The Pi 5 firmware initialises the PCIe link and maps RP1
//  to a fixed physical address *before* jumping to this kernel,
//  so we can access RP1 registers directly with pointer casts.
//
//  Wiring for this demo
//  --------------------
//  GPIO 21  (header pin 40)  ──[330 Ω]──  LED anode
//  GND      (header pin 39)  ──────────── LED cathode
// =============================================================

#include <stdint.h>

// ------------------------------------------------------------------
//  RP1 base address (PCIe BAR as mapped by the Pi 5 firmware)
// ------------------------------------------------------------------
#define RP1_BASE            0x1F00000000ULL

// ------------------------------------------------------------------
//  RP1 peripheral block offsets (from rp1-peripherals datasheet)
// ------------------------------------------------------------------
#define IO_BANK0_OFFSET     0x000D0000UL   // GPIO status / control
#define SYS_RIO0_OFFSET     0x000E0000UL   // Software-controlled I/O
#define PADS_BANK0_OFFSET   0x000F0000UL   // Pad drive / pull config

#define IO_BANK0_BASE       (RP1_BASE + IO_BANK0_OFFSET)
#define SYS_RIO0_BASE       (RP1_BASE + SYS_RIO0_OFFSET)
#define PADS_BANK0_BASE     (RP1_BASE + PADS_BANK0_OFFSET)

// ------------------------------------------------------------------
//  IO_BANK0 — per-pin registers (8 bytes each: STATUS then CTRL)
// ------------------------------------------------------------------
#define GPIO_STATUS(n)  (*(volatile uint32_t *)(IO_BANK0_BASE + (n)*8 + 0))
#define GPIO_CTRL(n)    (*(volatile uint32_t *)(IO_BANK0_BASE + (n)*8 + 4))

// ------------------------------------------------------------------
//  PADS_BANK0 — pad drive-strength / pull registers
//  GPIO 0 pad is at offset 0x04, GPIO 1 at 0x08, …
// ------------------------------------------------------------------
#define PAD_GPIO(n)     (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04 + (n)*4))

// ------------------------------------------------------------------
//  SYS_RIO0 — software output / output-enable registers
//
//  RP1 supports atomic register aliases (same as RP2040):
//    BASE + 0x0000  normal R/W
//    BASE + 0x1000  atomic XOR
//    BASE + 0x2000  atomic bitmask SET
//    BASE + 0x3000  atomic bitmask CLEAR
// ------------------------------------------------------------------
#define RIO_OUT         (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x0000 + 0x00))
#define RIO_OE          (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x0000 + 0x04))
#define RIO_OUT_SET     (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x2000 + 0x00))
#define RIO_OUT_CLR     (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x3000 + 0x00))
#define RIO_OE_SET      (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x2000 + 0x04))
#define RIO_OE_CLR      (*(volatile uint32_t *)(SYS_RIO0_BASE + 0x3000 + 0x04))

// GPIO_CTRL FUNCSEL value to route pin through SYS_RIO (software control)
#define GPIO_FUNC_SIO   5

// ------------------------------------------------------------------
//  PAD register bit positions
//  [7] OD  – Output Disable (0 = driver active)
//  [6] IE  – Input  Enable  (1 = input buffer on; lets you read back)
//  [5:4]   – Drive strength 00=2mA 01=4mA 10=8mA 11=12mA
//  [3] PUE – Pull-up enable
//  [2] PDE – Pull-down enable
//  [1]     – Schmitt trigger
//  [0]     – Slew-rate fast
// ------------------------------------------------------------------
#define PAD_OD          (1u << 7)
#define PAD_IE          (1u << 6)
#define PAD_DRIVE_4MA   (1u << 4)   // DRIVE = 01

// ------------------------------------------------------------------
//  LED GPIO pin
//  GPIO 21 = physical header pin 40 (convenient corner pin)
// ------------------------------------------------------------------
#define LED_GPIO        21


// =============================================================
//  gpio_set_output()  —  configure a pin as a driven output
// =============================================================
static void gpio_set_output(unsigned int pin)
{
    // 1. Configure pad: output driver on, 4 mA, input buffer on
    PAD_GPIO(pin) = PAD_IE | PAD_DRIVE_4MA;   // OD=0 → driver enabled

    // 2. Select SIO (software) function so RIO controls the pin
    GPIO_CTRL(pin) = GPIO_FUNC_SIO;

    // 3. Make sure the output starts LOW, then enable output driver
    RIO_OUT_CLR  = (1u << pin);
    RIO_OE_SET   = (1u << pin);
}

// =============================================================
//  gpio_write()  —  drive a pin high or low
// =============================================================
static void gpio_write(unsigned int pin, int value)
{
    if (value)
        RIO_OUT_SET = (1u << pin);
    else
        RIO_OUT_CLR = (1u << pin);
}

// =============================================================
//  gpio_toggle()  —  flip a pin (uses the atomic XOR alias)
// =============================================================
static void gpio_toggle(unsigned int pin)
{
    volatile uint32_t *rio_out_xor =
        (volatile uint32_t *)(SYS_RIO0_BASE + 0x1000 + 0x00);
    *rio_out_xor = (1u << pin);
}

// =============================================================
//  delay()  —  simple busy-wait
//  At Pi 5's ~2.4 GHz, 100 000 000 iterations ≈ 0.3–0.5 s
//  (exact timing depends on cache state; adjust to taste)
// =============================================================
static void delay(volatile uint64_t cycles)
{
    while (cycles--)
        __asm__ volatile ("nop");
}


// =============================================================
//  kernel_main()  —  your embedded system "super-loop"
// =============================================================
void kernel_main(void)
{
    // ---- one-time initialisation ----
    gpio_set_output(LED_GPIO);

    // ---- super-loop ----
    while (1)
    {
        gpio_write(LED_GPIO, 1);        // LED ON
        delay(100000000ULL);            // ~0.5 s

        gpio_write(LED_GPIO, 0);        // LED OFF
        delay(100000000ULL);            // ~0.5 s

        // ---------------------------------------------------------
        // YOUR APPLICATION CODE GOES HERE.
        // Treat this while(1) exactly like a microcontroller's
        // main loop — poll peripherals, update state, drive outputs.
        // ---------------------------------------------------------
    }
}
