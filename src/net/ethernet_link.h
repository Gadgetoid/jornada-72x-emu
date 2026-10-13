#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "core/machine.h"
#include "net/ethernet_gateway.h"

extern const uint8_t ETHERNET_LINK_MAC[6];

typedef struct {
    ethernet_gateway_t *gateway;
    net_gateway_log_fn log;
    const char *user_agent;
} ethernet_link_t;

void ethernet_link_init(ethernet_link_t *link, net_gateway_log_fn log, const char *user_agent);
bool ethernet_link_insert(ethernet_link_t *link, machine_t *machine);
void ethernet_link_pump(ethernet_link_t *link, machine_t *machine);
void ethernet_link_close(ethernet_link_t *link);
