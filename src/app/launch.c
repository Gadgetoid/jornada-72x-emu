#include "app/launch.h"

#include <stdlib.h>
#include <string.h>

void launch_parse(launch_t *launch, int argc, char **argv) {
    *launch = (launch_t){ .rom = "rom/jornada720.bin", .eeprom = "rom/jorn720_eeprom.bin" };
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fresh")) launch->fresh = true;
        else if (!strcmp(argv[i], "--optimisations")) launch->optimisations = true;
        else if (!strncmp(argv[i], "--state=", 8)) launch->state_file = argv[i] + 8;
        else if (!strncmp(argv[i], "--agent=", 8)) launch->agent_socket = argv[i] + 8;
        else if (!strcmp(argv[i], "--net")) launch->network = true;
        else if (!strncmp(argv[i], "--gdb=", 6)) launch->gdb_port = atoi(argv[i] + 6);
        else if (!strncmp(argv[i], "--gdb-process=", 14)) launch->gdb_process = argv[i] + 14;
        else if (!strcmp(argv[i], "--tcp")) launch->tcp_port = LAUNCH_TCP_PORT;
        else if (!strncmp(argv[i], "--tcp=", 6)) launch->tcp_port = atoi(argv[i] + 6);
        else if (positional++ == 0) launch->rom = argv[i];
        else launch->eeprom = argv[i];
    }
}
