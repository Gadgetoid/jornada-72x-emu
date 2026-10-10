#pragma once
#include <stdbool.h>
#include <stdint.h>

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
    bool poll_checked;
    bool poll_matched;
    bool poll_start_valid;
    uint32_t poll_start;
    uint64_t poll_deadline;
    uint64_t poll_skips;
    uint8_t filter[OPTIMISER_FILTER_BITS / 8];
} optimiser_t;

void optimiser_init(optimiser_t *optimiser, arm_cpu_t *cpu);
void optimiser_set_enabled(optimiser_t *optimiser, bool enabled);
void optimiser_set_deadline(optimiser_t *optimiser, uint64_t deadline);
bool optimiser_poll(optimiser_t *optimiser, uint32_t pa, uint32_t pc, uint32_t value, const sa1110_t *soc);