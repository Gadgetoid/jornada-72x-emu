#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/serial_link.h"

#define GDB_DEFAULT_PORT        1234
#define RAPI_DEFAULT_PORT       9990
#define SERIAL_TCP_DEFAULT_PORT 9991
#define SETTINGS_SCALE_COUNT    5
#ifdef __ANDROID__
#define SETTINGS_DEFAULT_OPTIMISATIONS 1
#else
#define SETTINGS_DEFAULT_OPTIMISATIONS 0
#endif

typedef struct {
    uint32_t speed;
    uint32_t optimisations;
    uint32_t host_time;
    uint32_t scale;
    char machine[64];
    uint32_t serial;
    char serial_device[SERIAL_LINK_PORT_NAME];
    char shared_folder[1024];
    uint32_t network_rapi, rapi_port;
    uint32_t gdb_server, gdb_port;
    uint32_t serial_tcp_port;
    char user_agent[256];
} settings_t;

settings_t settings_load(void);
void       settings_save(const settings_t *settings);
int        settings_scale_index(uint32_t scale);
uint32_t   settings_scale_at(int index);
