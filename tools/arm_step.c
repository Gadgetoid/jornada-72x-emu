#include "core/arm.h"

#include <stdio.h>
#include <string.h>

#define WINDOW_BASE 0x10000u
#define WINDOW_SIZE 0x4000u

static uint8_t window[WINDOW_SIZE];

static bool in_window(uint32_t pa, int size) {
    return pa >= WINDOW_BASE && pa + (uint32_t)size <= WINDOW_BASE + WINDOW_SIZE;
}

static bool bus_read(void *context, uint32_t pa, int size, uint32_t *value) {
    (void)context;
    if (!in_window(pa, size)) return false;
    *value = 0;
    memcpy(value, window + pa - WINDOW_BASE, (size_t)size);
    return true;
}

static bool bus_write(void *context, uint32_t pa, int size, uint32_t value) {
    (void)context;
    if (!in_window(pa, size)) return false;
    memcpy(window + pa - WINDOW_BASE, &value, (size_t)size);
    return true;
}

static uint8_t *bus_page(void *context, uint32_t pa, bool write) {
    (void)context;
    (void)write;
    if (!in_window(pa, 0x400)) return NULL;
    return window + pa - WINDOW_BASE;
}

int main(void) {
    static arm_cpu_t cpu;
    cpu.bus = (arm_bus_t){ .read = bus_read, .write = bus_write, .page = bus_page };
    uint32_t input[18];
    while (fread(input, sizeof input, 1, stdin) == 1 && fread(window, sizeof window, 1, stdin) == 1) {
        arm_reset(&cpu);
        arm_set_cpsr(&cpu, input[16]);
        memcpy(cpu.r, input, sizeof cpu.r);
        cpu.r[15] = WINDOW_BASE;
        memcpy(window, &input[17], 4);
        cpu.cycles = 0;
        arm_run(&cpu, 1);
        uint32_t output[18];
        memcpy(output, cpu.r, sizeof cpu.r);
        output[16] = cpu.cpsr;
        output[17] = cpu.undefined_count;
        fwrite(output, sizeof output, 1, stdout);
        fwrite(window, sizeof window, 1, stdout);
        fflush(stdout);
    }
    return 0;
}
