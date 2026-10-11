#include "native/native.h"
#include "core/arm.h"

#include <stdio.h>
#include <string.h>

#define MEMORY_BASE 0x10000000u
#define MEMORY_SIZE 16384u
#define TEST_COUNT 10000u
#define CE_TEST_COUNT 256u
#define CE_MEMMOVE_ADDRESS 0x01FD8A90u
#define CE_RETURN_ADDRESS 0x01FD9700u
#define CE_CODE_BASE 0x01FD8800u
#define CE_CODE_SIZE 0x1000u
#define CE_CODE_ROM_OFFSET 0x000B3800u
#define KERNEL_CODE_BASE 0x80075000u
#define KERNEL_CODE_SIZE 0x1000u
#define KERNEL_CODE_ROM_OFFSET 0x00075000u
#define CACHE_MAIN_BASE 0x85000000u
#define CACHE_MAIN_SIZE 0x2000u
#define CACHE_MINI_BASE 0x85100000u
#define CACHE_MINI_SIZE 0x400u
#define CACHE_SWEEP_ADDRESS 0x80075354u
#define CACHE_SWEEP_RETURN 0x80076000u

typedef struct {
    uint8_t memory[MEMORY_SIZE];
    uint32_t fail_write_at;
} test_memory_t;

static uint8_t ce_code[CE_CODE_SIZE];
static uint8_t kernel_code[KERNEL_CODE_SIZE];

static uint8_t *map_memory(void *context, uint32_t va, bool write) {
    test_memory_t *memory = context;
    if (va >= CACHE_MAIN_BASE && va - CACHE_MAIN_BASE < CACHE_MAIN_SIZE)
        return memory->memory + (va - CACHE_MAIN_BASE);
    if (va >= CACHE_MINI_BASE && va - CACHE_MINI_BASE < CACHE_MINI_SIZE)
        return memory->memory + CACHE_MAIN_SIZE + (va - CACHE_MINI_BASE);
    if (va < MEMORY_BASE || va - MEMORY_BASE >= MEMORY_SIZE) return NULL;
    if (write && memory->fail_write_at && va >= memory->fail_write_at) return NULL;
    return memory->memory + (va - MEMORY_BASE);
}

static bool read_memory(void *context, uint32_t pa, int size, uint32_t *value) {
    uint8_t *host = map_memory(context, pa, false);
    if (!host) return false;
    *value = 0;
    memcpy(value, host, (size_t)size);
    return true;
}

static bool write_memory(void *context, uint32_t pa, int size, uint32_t value) {
    uint8_t *host = map_memory(context, pa, true);
    if (!host) return false;
    memcpy(host, &value, (size_t)size);
    return true;
}

static uint8_t *page_memory(void *context, uint32_t pa, bool write) {
    if (pa >= CE_CODE_BASE && pa - CE_CODE_BASE < CE_CODE_SIZE) return write ? NULL : ce_code + (pa - CE_CODE_BASE);
    if (pa >= KERNEL_CODE_BASE && pa - KERNEL_CODE_BASE < KERNEL_CODE_SIZE)
        return write ? NULL : kernel_code + (pa - KERNEL_CODE_BASE);
    return map_memory(context, pa, write);
}

static bool stop_at_return(void *context, uint32_t pc) {
    return pc == *(const uint32_t *)context;
}

static uint32_t random_next(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int check_random_cases(void) {
    test_memory_t actual = { 0 }, expected = { 0 };
    native_memory_t memory = { .context = &actual, .map = map_memory };
    uint32_t random_state = 0x72E4A11u;
    for (uint32_t test = 0; test < TEST_COUNT; test++) {
        uint32_t source_offset = random_next(&random_state) % 8192;
        uint32_t distance = random_next(&random_state) % 4096;
        uint32_t destination_offset = (random_next(&random_state) & 1) ? source_offset + distance :
                                      source_offset > distance ? source_offset - distance : source_offset;
        uint32_t length = random_next(&random_state) % 4097;
        if (source_offset + length >= MEMORY_SIZE || destination_offset + length >= MEMORY_SIZE) return 1;
        for (uint32_t i = 0; i < MEMORY_SIZE; i++) {
            uint8_t value = (uint8_t)random_next(&random_state);
            actual.memory[i] = value;
            expected.memory[i] = value;
        }
        memmove(expected.memory + destination_offset, expected.memory + source_offset, length);
        uint32_t arguments[4] = { MEMORY_BASE + destination_offset, MEMORY_BASE + source_offset, length, 0 };
        native_result_t result = { 0 };
        if (!native_memmove(&memory, arguments, &result) || result.value != arguments[0] ||
            memcmp(actual.memory, expected.memory, MEMORY_SIZE)) {
            fprintf(stderr, "memmove mismatch at case %u\n", test);
            return 1;
        }
    }
    return 0;
}

static int check_data_tlb_mapping(void) {
    static arm_cpu_t cpu;
    test_memory_t memory = { 0 };
    cpu.bus = (arm_bus_t){ .context = &memory, .read = read_memory, .write = write_memory, .page = page_memory };
    arm_reset(&cpu);
    cpu.bus.context = &memory;
    cpu.control = ARM_CONTROL_M;
    cpu.ttb = MEMORY_BASE;
    cpu.dacr = 3;
    uint32_t section = MEMORY_BASE | 2;
    memcpy(memory.memory + 8, &section, sizeof section);
    uint32_t va = 0x00200100u;
    uint8_t *mapped = arm_map_virtual(&cpu, va, false, true);
    bool filled = false;
    for (int i = 0; i < ARM_TLB_ENTRIES; i++) {
        arm_tlb_entry_t *entry = &cpu.data_tlb.entries[i];
        if (entry->serial && (va & ~entry->mask) == entry->base) filled = true;
    }
    if (mapped != memory.memory + 0x100 || !filled || arm_map_virtual(&cpu, 0x00300100u, false, true)) {
        fprintf(stderr, "native mapping did not fill or respect the data TLB\n");
        return 1;
    }
    printf("native map: data-TLB miss filled and unmapped section rejected\n");
    return 0;
}

static int check_fallback_is_atomic(void) {
    test_memory_t actual = { .fail_write_at = MEMORY_BASE + NATIVE_PAGE };
    native_memory_t memory = { .context = &actual, .map = map_memory };
    memset(actual.memory, 0x5A, sizeof actual.memory);
    uint8_t original[MEMORY_SIZE];
    memcpy(original, actual.memory, sizeof original);
    uint32_t arguments[4] = { MEMORY_BASE + NATIVE_PAGE - 4, MEMORY_BASE, 32, 0 };
    native_result_t result = { 0 };
    if (native_memmove(&memory, arguments, &result) || memcmp(actual.memory, original, sizeof original)) {
        fprintf(stderr, "unmapped destination did not fall back atomically\n");
        return 1;
    }
    return 0;
}

static int check_ce_cases(void) {
    FILE *rom = fopen("rom/jornada720.bin", "rb");
    if (!rom || fseek(rom, CE_CODE_ROM_OFFSET, SEEK_SET) || fread(ce_code, 1, sizeof ce_code, rom) != sizeof ce_code) {
        if (rom) fclose(rom);
        fprintf(stderr, "cannot load coredll memmove code from ROM\n");
        return 1;
    }
    fclose(rom);

    static arm_cpu_t cpu;
    test_memory_t native_memory_state = { 0 }, ce_memory_state = { 0 };
    native_memory_t memory = { .context = &native_memory_state, .map = map_memory };
    cpu.bus = (arm_bus_t){ .context = &ce_memory_state, .read = read_memory, .write = write_memory, .page = page_memory };
    uint32_t random_state = 0xC0DE720u;
    uint64_t total_cycle_error = 0, total_ce_instructions = 0, total_native_cycles = 0;
    uint64_t size_ce_instructions[3] = { 0 }, size_native_cycles[3] = { 0 }, size_counts[3] = { 0 };
    for (uint32_t test = 0; test < CE_TEST_COUNT; test++) {
        uint32_t source_offset = random_next(&random_state) % 4096;
        uint32_t destination_offset = random_next(&random_state) % 4096;
        uint32_t length = random_next(&random_state) % 257;
        if (source_offset + length >= 8192 || destination_offset + length >= 8192) return 1;
        for (uint32_t i = 0; i < MEMORY_SIZE; i++) native_memory_state.memory[i] = (uint8_t)random_next(&random_state);
        memcpy(ce_memory_state.memory, native_memory_state.memory, MEMORY_SIZE);

        uint32_t arguments[4] = { MEMORY_BASE + destination_offset, MEMORY_BASE + source_offset, length, 0 };
        native_result_t result = { 0 };
        if (!native_memmove(&memory, arguments, &result)) {
            fprintf(stderr, "native memmove rejected CE test case %u\n", test);
            return 1;
        }

        uint32_t return_address = CE_RETURN_ADDRESS;
        arm_reset(&cpu);
        cpu.bus.context = &ce_memory_state;
        cpu.hook_context = &return_address;
        cpu.before = stop_at_return;
        arm_set_cpsr(&cpu, ARM_MODE_USR);
        cpu.r[0] = arguments[0];
        cpu.r[1] = arguments[1];
        cpu.r[2] = arguments[2];
        for (int reg = 4; reg <= 11; reg++) cpu.r[reg] = 0xA5000000u | (uint32_t)reg;
        cpu.r[13] = MEMORY_BASE + MEMORY_SIZE - 16;
        cpu.r[14] = return_address;
        cpu.r[15] = CE_MEMMOVE_ADDRESS;
        cpu.cycles = 0;
        cpu.instructions = 0;
        arm_run(&cpu, 100000);
        if (cpu.r[15] != return_address || cpu.r[0] != arguments[0] ||
            memcmp(native_memory_state.memory, ce_memory_state.memory, 8192)) {
            fprintf(stderr, "CE/native memmove mismatch at case %u\n", test);
            return 1;
        }
        if (cpu.r[13] != MEMORY_BASE + MEMORY_SIZE - 16 || (cpu.cpsr & ARM_PSR_MODE) != ARM_MODE_USR) {
            fprintf(stderr, "CE memmove SP/flags case %u: sp %08x cpsr %08x\n", test, cpu.r[13], cpu.cpsr);
            return 1;
        }
        for (int reg = 4; reg <= 11; reg++) {
            if (cpu.r[reg] != (0xA5000000u | (uint32_t)reg)) {
                fprintf(stderr, "CE memmove changed r%d at case %u\n", reg, test);
                return 1;
            }
        }
        uint64_t error = result.cycles > cpu.instructions ? result.cycles - cpu.instructions :
                         cpu.instructions - result.cycles;
        total_cycle_error += error;
        total_ce_instructions += cpu.instructions;
        total_native_cycles += result.cycles;
        int size_class = length < 8 ? 0 : length < 32 ? 1 : 2;
        size_ce_instructions[size_class] += cpu.instructions;
        size_native_cycles[size_class] += result.cycles;
        size_counts[size_class]++;
    }
    printf("CE memmove: %u randomized cases passed; CE %llu, native %llu, mean absolute error %.1f; size classes",
           CE_TEST_COUNT, (unsigned long long)total_ce_instructions, (unsigned long long)total_native_cycles,
           (double)total_cycle_error / CE_TEST_COUNT);
    for (int size_class = 0; size_class < 3; size_class++)
        printf(" %d:%llu/%llu err%llu", size_class, (unsigned long long)size_ce_instructions[size_class] / size_counts[size_class],
               (unsigned long long)size_native_cycles[size_class] / size_counts[size_class],
               (unsigned long long)(size_native_cycles[size_class] > size_ce_instructions[size_class]
                             ? size_native_cycles[size_class] - size_ce_instructions[size_class]
                             : size_ce_instructions[size_class] - size_native_cycles[size_class]) / size_counts[size_class]);
    printf("\n");
    return 0;
}

static int check_cache_sweep(void) {
    FILE *rom = fopen("rom/jornada720.bin", "rb");
    if (!rom || fseek(rom, KERNEL_CODE_ROM_OFFSET, SEEK_SET) ||
        fread(kernel_code, 1, sizeof kernel_code, rom) != sizeof kernel_code) {
        if (rom) fclose(rom);
        fprintf(stderr, "cannot load kernel cache-sweep code from ROM\n");
        return 1;
    }
    fclose(rom);

    static arm_cpu_t cpu;
    test_memory_t native_memory_state = { 0 }, ce_memory_state = { 0 };
    native_memory_t memory = { .context = &native_memory_state, .map = map_memory };
    cpu.bus = (arm_bus_t){ .context = &ce_memory_state, .read = read_memory, .write = write_memory, .page = page_memory };
    uint32_t random_state = 0xCACE1110u;
    for (uint32_t test = 0; test < 32; test++) {
        for (uint32_t i = 0; i < sizeof native_memory_state.memory; i++)
            native_memory_state.memory[i] = (uint8_t)random_next(&random_state);
        memcpy(ce_memory_state.memory, native_memory_state.memory, sizeof native_memory_state.memory);
        uint32_t last_value;
        if (!native_cache_sweep(&memory, &last_value)) {
            fprintf(stderr, "native cache sweep rejected mapped test pages\n");
            return 1;
        }

        uint32_t return_address = CACHE_SWEEP_RETURN;
        arm_reset(&cpu);
        cpu.bus.context = &ce_memory_state;
        cpu.hook_context = &return_address;
        cpu.before = stop_at_return;
        arm_set_cpsr(&cpu, ARM_MODE_SVC);
        for (int reg = 3; reg <= 12; reg++) cpu.r[reg] = 0xA5110000u | (uint32_t)reg;
        cpu.r[13] = MEMORY_BASE + MEMORY_SIZE - 16;
        cpu.r[14] = return_address;
        cpu.r[15] = CACHE_SWEEP_ADDRESS;
        cpu.cycles = 0;
        cpu.instructions = 0;
        arm_run(&cpu, 10000);
        if (cpu.r[15] != return_address || cpu.r[0] != 0x85100400u || cpu.r[1] != 0 || cpu.r[2] != last_value ||
            cpu.r[13] != MEMORY_BASE + MEMORY_SIZE - 16 || cpu.instructions != 870 ||
            (cpu.cpsr & (ARM_PSR_N | ARM_PSR_Z | ARM_PSR_C | ARM_PSR_V)) != (ARM_PSR_Z | ARM_PSR_C) ||
            memcmp(native_memory_state.memory, ce_memory_state.memory, sizeof native_memory_state.memory)) {
            fprintf(stderr, "CE/native cache-sweep mismatch %u: pc %08x r0 %08x r1 %08x r2 %08x expected %08x "
                    "sp %08x cpsr %08x instructions %llu memory %d\n", test, cpu.r[15], cpu.r[0], cpu.r[1], cpu.r[2],
                    last_value, cpu.r[13], cpu.cpsr, (unsigned long long)cpu.instructions,
                    memcmp(native_memory_state.memory, ce_memory_state.memory, sizeof native_memory_state.memory));
            return 1;
        }
        for (int reg = 3; reg <= 12; reg++) {
            if (cpu.r[reg] != (0xA5110000u | (uint32_t)reg)) {
                fprintf(stderr, "CE cache sweep changed r%d at case %u\n", reg, test);
                return 1;
            }
        }
    }
    printf("CE cache sweep: 32 randomized cases passed; 870 instructions preserved\n");
    return 0;
}

int main(void) {
    if (check_random_cases() || check_data_tlb_mapping() || check_fallback_is_atomic() || check_ce_cases() ||
        check_cache_sweep()) return 1;
    printf("native memmove: %u libc cases and atomic fallback passed\n", TEST_COUNT);
    return 0;
}