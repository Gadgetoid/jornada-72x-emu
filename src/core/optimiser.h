#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "core/arm.h"
#include "core/sa1110.h"

#define OPTIMISER_FILTER_BITS 8192u

typedef struct {
    arm_cpu_t *cpu;
    bool enabled;
    bool memmove_checked;
    bool memmove_matched;
    uint64_t memmove_entries;
    uint64_t memmove_fallbacks;
    uint64_t memmove_hits;
    uint64_t memmove_guest_cycles_charged;
    bool cache_sweep_checked;
    bool cache_sweep_matched;
    uint64_t cache_sweep_entries;
    uint64_t cache_sweep_fallbacks;
    uint64_t cache_sweep_hits;
    uint64_t cache_sweep_instructions_skipped;
    bool poll_checked;
    bool poll_matched;
    bool poll_start_valid;
    uint32_t poll_start;
    uint64_t poll_deadline;
    uint64_t poll_skips;
    uint64_t poll_cycles_advanced;
    uint8_t filter[OPTIMISER_FILTER_BITS / 8];
    uint64_t tick_spin_skips;
} optimiser_t;

void optimiser_init(optimiser_t *optimiser, arm_cpu_t *cpu);
void optimiser_set_enabled(optimiser_t *optimiser, bool enabled);
void optimiser_set_deadline(optimiser_t *optimiser, uint64_t deadline);
void optimiser_print(const optimiser_t *optimiser, FILE *out);
bool optimiser_poll(optimiser_t *optimiser, uint32_t pa, uint32_t pc, uint32_t value, const sa1110_t *soc);