#pragma once
#include <stdbool.h>

#include "app/settings.h"
#include "core/machine.h"
#include "net/ethernet_link.h"

typedef struct {
    ethernet_link_t link;
} ethernet_service_t;

void        ethernet_service_init(ethernet_service_t *service, const settings_t *settings);
bool        ethernet_service_available(machine_t *machine);
const char *ethernet_service_toggle(ethernet_service_t *service, machine_t *machine);
void        ethernet_service_close(ethernet_service_t *service);
