#pragma once

#include <stdbool.h>

#define LAUNCH_TCP_PORT 9991

typedef struct {
    const char *rom, *eeprom, *state_file, *agent_socket, *gdb_process;
    int gdb_port;
    bool fresh, optimisations, network;
    int tcp_port;
} launch_t;

void launch_parse(launch_t *launch, int argc, char **argv);
