#include "block_test.h"

#include <stdint.h>

#include "block.h"
#include "uart/uart.h"

#define BLOCK_TEST_MAGIC       UINT32_C(0x53444254)
#define BLOCK_TEST_VERSION     UINT32_C(1)
#define BLOCK_TEST_SECTOR_SIZE UINT32_C(512)
#define BLOCK_TEST_WORDS       (BLOCK_TEST_SECTOR_SIZE / sizeof(uint32_t))
#define BLOCK_TEST_SEED        UINT32_C(0x5480cafe)

static uint32_t expected_block[BLOCK_TEST_WORDS] __attribute__((aligned(16)));
static uint32_t read_block_buf[BLOCK_TEST_WORDS] __attribute__((aligned(16)));

static void print_label_hex(const char *label, uint64_t value) {
    uart_puts(label);
    uart_puthex(value);
    uart_puts("\n");
}

static uint32_t mix_word(uint32_t value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static uint32_t checksum_words(const uint32_t *words) {
    uint32_t checksum = UINT32_C(2166136261);

    for (uint32_t i = 0; i < BLOCK_TEST_WORDS; i++) {
        uint32_t word = (i == 7) ? 0 : words[i];
        checksum ^= word;
        checksum *= UINT32_C(16777619);
    }

    return checksum;
}

static void fill_expected_block(uint64_t lba) {
    uint32_t seed = BLOCK_TEST_SEED ^ (uint32_t)lba ^ (uint32_t)(lba >> 32);

    for (uint32_t i = 0; i < BLOCK_TEST_WORDS; i++) {
        seed = mix_word(seed + i + UINT32_C(0x9e3779b9));
        expected_block[i] = seed ^ (UINT32_C(0x10010000) + i);
    }

    expected_block[0] = BLOCK_TEST_MAGIC;
    expected_block[1] = BLOCK_TEST_VERSION;
    expected_block[2] = (uint32_t)lba;
    expected_block[3] = (uint32_t)(lba >> 32);
    expected_block[4] = BLOCK_TEST_SECTOR_SIZE;
    expected_block[5] = BLOCK_TEST_SEED;
    expected_block[6] = UINT32_C(0x52454144);
    expected_block[7] = checksum_words(expected_block);
}

static int blocks_match(const uint32_t *actual, const uint32_t *expected) {
    for (uint32_t i = 0; i < BLOCK_TEST_WORDS; i++) {
        if (actual[i] != expected[i]) {
            uart_puts("[block-test] mismatch word=");
            uart_puthex(i);
            uart_puts(" actual=");
            uart_puthex(actual[i]);
            uart_puts(" expected=");
            uart_puthex(expected[i]);
            uart_puts("\n");
            return 0;
        }
    }

    return 1;
}

static int read_and_verify(uint64_t lba) {
    if (block_read(lba, 1, read_block_buf) != 0) {
        uart_puts("[block-test] block_read failed\n");
        return -1;
    }

    if (read_block_buf[0] != BLOCK_TEST_MAGIC) {
        print_label_hex("[block-test] missing magic, word0=", read_block_buf[0]);
        return -1;
    }

    if (read_block_buf[7] != checksum_words(read_block_buf)) {
        print_label_hex("[block-test] checksum mismatch stored=", read_block_buf[7]);
        print_label_hex("[block-test] checksum computed=", checksum_words(read_block_buf));
        return -1;
    }

    fill_expected_block(lba);
    if (!blocks_match(read_block_buf, expected_block)) {
        return -1;
    }

    print_label_hex("[block-test] verified checksum=", read_block_buf[7]);
    return 0;
}

void block_test_write_read(uint64_t lba) {
    uart_puts("[block-test] begin write-read\n");
    print_label_hex("[block-test] scratch lba=", lba);
    uart_puts("[block-test] WARNING: overwriting exactly one scratch sector\n");

    fill_expected_block(lba);
    print_label_hex("[block-test] writing checksum=", expected_block[7]);

    if (block_write(lba, 1, expected_block) != 0) {
        uart_puts("[block-test] block_write failed\n");
        return;
    }

    if (read_and_verify(lba) == 0) {
        uart_puts("[block-test] PASS write-read\n");
        uart_puts("[block-test] power off, unplug/replug, then run block_test_verify_persistence with the same LBA\n");
    } else {
        uart_puts("[block-test] FAIL write-read\n");
    }
}

void block_test_verify_persistence(uint64_t lba) {
    uart_puts("[block-test] begin persistence-verify\n");
    print_label_hex("[block-test] scratch lba=", lba);

    if (read_and_verify(lba) == 0) {
        uart_puts("[block-test] PASS persistence\n");
    } else {
        uart_puts("[block-test] FAIL persistence\n");
    }
}
