#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "core/machine.h"

typedef enum {
    STATE_OK,
    STATE_MISSING,
    STATE_UNREADABLE,
    STATE_INCOMPATIBLE,
    STATE_WRONG_ROM,
} state_result_t;

bool state_save(const machine_t *machine, const char *path, uint64_t host_time);
state_result_t state_load(machine_t *machine, const char *path, uint64_t host_time);
const char *state_describe(state_result_t result);
