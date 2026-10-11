#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/notices.h"
#include "app/settings.h"
#include "core/machine.h"
#include "net/serial_link.h"

#define SERIAL_PORT_MAX 16

typedef struct {
    serial_link_t link;
    settings_t *settings;
    char rapi_socket[1024];
    char notice[320];
    uint64_t plug_at, unplug_at, port_scan_at;
    bool tcp_attached;
    char ports[SERIAL_PORT_MAX][SERIAL_LINK_PORT_NAME];
    int port_count;
} serial_service_t;

void        serial_service_init(serial_service_t *service, settings_t *settings);
const char *serial_service_start(serial_service_t *service, machine_t *machine);
const char *serial_service_select(serial_service_t *service, machine_t *machine, serial_mode_t mode, const char *device);
void        serial_service_reattach(serial_service_t *service, machine_t *machine);
void        serial_service_attach_machine(serial_service_t *service, machine_t *machine);
void        serial_service_soft_reset(serial_service_t *service, machine_t *machine);
void        serial_service_replug(serial_service_t *service, machine_t *machine, uint64_t seconds);
void        serial_service_update_rapi(serial_service_t *service, machine_t *machine);
bool        serial_service_online(const serial_service_t *service);
void        serial_service_step(serial_service_t *service, machine_t *machine, notice_t *notice);
void        serial_service_close(serial_service_t *service);
