#pragma once

#include <stdbool.h>

typedef struct {
    const char *rom, *eeprom, *state_file, *agent_socket;
    bool fresh, optimisations;
} launch_t;

void launch_parse(launch_t *launch, int argc, char **argv);
