#include "core/optimiser.h"

#include <string.h>

#include "native/native.h"

#define MEMMOVE_ADDRESS 0x01FD8A90u
#define MEMMOVE_WORDS 12u
#define CACHE_SWEEP_ADDRESS 0x80075354u
#define CACHE_SWEEP_WORDS 12u
#define CACHE_SWEEP_CYCLES 870u
#define OSCR_DELAY_ADDRESS 0x0004F06Cu
#define OSCR_START_PC 0x0004F080u
#define OSCR_DELAY_PC 0x0004F088u
#define OSCR_ADDRESS 0x90000010u
#define OSCR_DELAY_WORDS 12u

static const uint32_t oscr_delay_code[OSCR_DELAY_WORDS] = {
    0xE3A03C75u, 0xE38330F7u, 0xE3A01209u, 0xE0030390u, 0xE3811010u, 0xE5912000u,
    0xE1A006A3u, 0xE5913000u, 0xE0433002u, 0xE1530000u, 0x3AFFFFFBu, 0xE1A0F00Eu,
};

static const uint32_t memmove_code[MEMMOVE_WORDS] = {
    0xE92D4031u, 0xE1510000u, 0xAA0000F2u, 0xE0811002u, 0xE0800002u, 0xE3520008u,
    0xBA0000D7u, 0xE2103003u, 0x0A000007u, 0xE5714001u, 0xE3530002u, 0xE5604001u,
};

static const uint32_t cache_sweep_code[CACHE_SWEEP_WORDS] = {
    0xE3A00485u, 0xE3A01C01u, 0xE4902020u, 0xE2511001u, 0x1AFFFFFCu, 0xE3A00485u,
    0xE2800601u, 0xE3A01020u, 0xE4902020u, 0xE2511001u, 0x1AFFFFFCu, 0xE1A0F00Eu,
};

static bool read_code(arm_cpu_t *cpu, uint32_t pc, uint32_t *code, uint32_t words) {
    for (uint32_t i = 0; i < words; i++)
        if (!arm_read_instruction_virtual(cpu, pc + i * 4, &code[i])) return false;
    return true;
}

static uint8_t *native_map(void *context, uint32_t va, bool write) {
    arm_cpu_t *cpu = context;
    bool user = (cpu->cpsr & ARM_PSR_MODE) == ARM_MODE_USR;
    return arm_map_virtual(cpu, va, write, user);
}

static bool optimiser_run(void *context, arm_cpu_t *cpu, uint32_t pc) {
    optimiser_t *optimiser = context;
    if (pc == CACHE_SWEEP_ADDRESS) {
        optimiser->cache_sweep_entries++;
        if (!optimiser->cache_sweep_checked) {
            uint32_t code[CACHE_SWEEP_WORDS];
            if (!read_code(cpu, pc, code, CACHE_SWEEP_WORDS)) return false;
            optimiser->cache_sweep_matched = memcmp(code, cache_sweep_code, sizeof code) == 0;
            optimiser->cache_sweep_checked = true;
        }
        if (!optimiser->cache_sweep_matched) return false;
        native_memory_t memory = { .context = cpu, .map = native_map };
        uint32_t last_value;
        if (optimiser->poll_deadline <= cpu->cycles ||
            CACHE_SWEEP_CYCLES > optimiser->poll_deadline - cpu->cycles ||
            !native_cache_sweep(&memory, &last_value)) {
            optimiser->cache_sweep_fallbacks++;
            return false;
        }
        optimiser->cache_sweep_hits++;
        optimiser->cache_sweep_instructions_skipped += CACHE_SWEEP_CYCLES;
        cpu->r[0] = 0x85100400u;
        cpu->r[1] = 0;
        cpu->r[2] = last_value;
        cpu->cpsr = (cpu->cpsr & ~(ARM_PSR_N | ARM_PSR_Z | ARM_PSR_C | ARM_PSR_V)) | ARM_PSR_Z | ARM_PSR_C;
        cpu->r[15] = cpu->r[14];
        cpu->cycles += CACHE_SWEEP_CYCLES - 1;
        cpu->instructions += CACHE_SWEEP_CYCLES;
        return true;
    }
    if ((pc & 0x01FFFFFFu) != MEMMOVE_ADDRESS) return false;
    optimiser->memmove_entries++;
    if (!optimiser->memmove_checked) {
        uint32_t code[MEMMOVE_WORDS];
        if (!read_code(cpu, pc, code, MEMMOVE_WORDS)) return false;
        optimiser->memmove_matched = memcmp(code, memmove_code, sizeof code) == 0;
        optimiser->memmove_checked = true;
    }
    if (!optimiser->memmove_matched) return false;
    native_memory_t memory = { .context = cpu, .map = native_map };
    uint32_t arguments[4] = { cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3] };
    uint64_t estimated_cycles = native_memmove_cycles(arguments[2]);
    if (optimiser->poll_deadline <= cpu->cycles || estimated_cycles > optimiser->poll_deadline - cpu->cycles)
        return false;
    native_result_t result = { 0 };
    if (!native_memmove(&memory, arguments, &result) || !result.cycles) {
        optimiser->memmove_fallbacks++;
        return false;
    }
    optimiser->memmove_hits++;
    optimiser->memmove_guest_cycles_charged += result.cycles;
    cpu->r[0] = result.value;
    cpu->r[15] = cpu->r[14];
    cpu->cycles += result.cycles - 1;
    cpu->instructions += result.cycles;
    return true;
}

void optimiser_init(optimiser_t *optimiser, arm_cpu_t *cpu) {
    memset(optimiser, 0, sizeof *optimiser);
    optimiser->cpu = cpu;
    uint32_t bit = (MEMMOVE_ADDRESS >> 2) & (OPTIMISER_FILTER_BITS - 1);
    optimiser->filter[bit >> 3] |= 1u << (bit & 7);
    bit = (CACHE_SWEEP_ADDRESS >> 2) & (OPTIMISER_FILTER_BITS - 1);
    optimiser->filter[bit >> 3] |= 1u << (bit & 7);
    cpu->optimiser_context = optimiser;
    cpu->on_optimiser = optimiser_run;
}

void optimiser_set_enabled(optimiser_t *optimiser, bool enabled) {
    optimiser->enabled = enabled;
    optimiser->cpu->optimiser_filter = enabled ? optimiser->filter : NULL;
}

void optimiser_set_deadline(optimiser_t *optimiser, uint64_t deadline) {
    optimiser->poll_deadline = deadline;
}

bool optimiser_poll(optimiser_t *optimiser, uint32_t pa, uint32_t pc, uint32_t value, const sa1110_t *soc) {
    if (!optimiser->enabled || pa != OSCR_ADDRESS || (pc != OSCR_START_PC && pc != OSCR_DELAY_PC)) return false;
    arm_cpu_t *cpu = optimiser->cpu;
    if (!optimiser->poll_checked) {
        uint32_t code[OSCR_DELAY_WORDS];
        if (!read_code(cpu, OSCR_DELAY_ADDRESS, code, OSCR_DELAY_WORDS)) return false;
        optimiser->poll_matched = memcmp(code, oscr_delay_code, sizeof code) == 0;
        optimiser->poll_checked = true;
    }
    if (!optimiser->poll_matched) return false;
    if (pc == OSCR_START_PC) {
        optimiser->poll_start = value;
        optimiser->poll_start_valid = true;
        return false;
    }
    if (pc != OSCR_DELAY_PC || !optimiser->poll_start_valid) return false;
    uint32_t target = cpu->r[0];
    uint32_t elapsed = value - optimiser->poll_start;
    if (elapsed >= target) {
        optimiser->poll_start_valid = false;
        return false;
    }
    uint64_t deadline = sa1110_oscr_deadline(soc, cpu->cycles, target - elapsed);
    uint64_t resume = deadline > 4 ? deadline - 4 : cpu->cycles;
    if (resume > optimiser->poll_deadline) resume = optimiser->poll_deadline;
    if (resume <= cpu->cycles) return false;
    optimiser->poll_cycles_advanced += resume - cpu->cycles;
    cpu->cycles = resume;
    optimiser->poll_skips++;
    return true;
}

void optimiser_print(const optimiser_t *optimiser, FILE *out) {
    fprintf(out, "native memmove: entries %llu, fingerprint %s, fallbacks %llu, hits %llu\n",
            (unsigned long long)optimiser->memmove_entries,
            optimiser->memmove_checked ? (optimiser->memmove_matched ? "matched" : "mismatched") : "unchecked",
            (unsigned long long)optimiser->memmove_fallbacks, (unsigned long long)optimiser->memmove_hits);
    fprintf(out, "native cache sweep: entries %llu, fingerprint %s, fallbacks %llu, hits %llu\n",
            (unsigned long long)optimiser->cache_sweep_entries,
            optimiser->cache_sweep_checked ? (optimiser->cache_sweep_matched ? "matched" : "mismatched") : "unchecked",
            (unsigned long long)optimiser->cache_sweep_fallbacks, (unsigned long long)optimiser->cache_sweep_hits);
    fprintf(out, "native OSCR delay: fingerprint %s, poll skips %llu\n",
            optimiser->poll_checked ? (optimiser->poll_matched ? "matched" : "mismatched") : "unchecked",
            (unsigned long long)optimiser->poll_skips);
    fprintf(out, "expected work: cache sweep skipped %llu ARM instructions; memmove charged %llu estimated ARM cycles; "
            "OSCR advanced %llu guest cycles\n",
            (unsigned long long)optimiser->cache_sweep_instructions_skipped,
            (unsigned long long)optimiser->memmove_guest_cycles_charged,
            (unsigned long long)optimiser->poll_cycles_advanced);
}
