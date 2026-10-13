#pragma once

#include <stdbool.h>

#include "app/settings.h"
#include "util/options.h"

typedef struct {
    settings_t   *settings;
    const char   *rom, *eeprom, *state_file, *machine;
    bool fresh, verbose;
    int gdb_port;
    int ir_tcp_port;
    const char   *gdb_process;
    const char   *card, *pc_card;
    const char   *agent_socket;
} launch_t;

options_result_t launch_parse(launch_t *launch, settings_t *settings, int argc, char **argv);
