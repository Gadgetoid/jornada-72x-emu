#pragma once

#include <stdbool.h>

#include "app/notices.h"
#include "core/machine.h"
#include "net/serial_link.h"

typedef struct {
    serial_link_t link;
    serial_mode_t mode;
    char rapi_socket[1024];
    bool tcp_attached;
} serial_service_t;

void        serial_service_init(serial_service_t *service, serial_mode_t mode, int tcp_port, net_gateway_log_fn log);
const char *serial_service_start(serial_service_t *service, machine_t *machine);
bool        serial_service_online(const serial_service_t *service);
bool        serial_service_take_desktop(serial_service_t *service);
void        serial_service_step(serial_service_t *service, machine_t *machine, notice_t *notice);
void        serial_service_close(serial_service_t *service);
