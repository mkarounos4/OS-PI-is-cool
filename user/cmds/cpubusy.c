#include "lib/errno.h"
#include "lib/malloc.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/syscall.h"

#define DEFAULT_TICKS 1000u
#define SCRATCH_SIZE 256u

static uint32_t parse_ticks(const char *s, int *ok) {
    char *end;
    long value = strtol(s, &end, 10);
    if (s == end || *end != '\0' || value < 0) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return (uint32_t)value;
}

int main(int argc, char **argv) {
    uint32_t run_ticks = DEFAULT_TICKS;
    const char *tag = "busy";

    if (argc > 1) {
        int ok;
        run_ticks = parse_ticks(argv[1], &ok);
        if (!ok) {
            print_errno("cpubusy", "usage: cpubusy [timer_ticks] [tag]", -EINVAL);
            return -EINVAL;
        }
    }
    if (argc > 2 && argv[2] != NULL) {
        tag = argv[2];
    }

    volatile uint32_t checksum = 0x12345678u;
    unsigned char *scratch = malloc(SCRATCH_SIZE);
    if (scratch != NULL) {
        for (unsigned int i = 0; i < SCRATCH_SIZE; i++) {
            scratch[i] = (unsigned char)i;
        }
    }

    uint32_t start = (uint32_t)get_ticks();
    uint32_t last = start;
    printf("cpubusy start tag=%s pid=%u ticks=%u\n",
           tag, (unsigned int)getpid(), (unsigned int)run_ticks);

    while (run_ticks == 0 || (uint32_t)(last - start) < run_ticks) {
        for (unsigned int i = 0; i < 4096; i++) {
            checksum = (checksum << 5) ^ (checksum >> 2) ^ i ^ (uint32_t)(uintptr_t)&checksum;
            if (scratch != NULL) {
                scratch[i & (SCRATCH_SIZE - 1u)] ^= (unsigned char)checksum;
            }
        }
        last = (uint32_t)get_ticks();
    }

    if (scratch != NULL) {
        checksum ^= scratch[0];
        free(scratch);
    }

    printf("cpubusy done tag=%s pid=%u elapsed_ticks=%u checksum=%x\n",
           tag, (unsigned int)getpid(), (unsigned int)(last - start),
           (unsigned int)checksum);
    return 0;
}
