#include "net/ethernet_link.h"

#include <string.h>

#include "core/ne2000.h"

const uint8_t ETHERNET_LINK_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };

void ethernet_link_init(ethernet_link_t *link, net_gateway_log_fn log, const char *user_agent) {
    memset(link, 0, sizeof *link);
    link->log = log;
    link->user_agent = user_agent ? user_agent : NET_GATEWAY_DEFAULT_USER_AGENT;
}

static bool open_gateway(ethernet_link_t *link) {
    if (link->gateway) return true;
    net_gateway_options_t options = { link->user_agent, NULL, 0 };
    link->gateway = ethernet_gateway_create(link->log, &options);
    return link->gateway != NULL;
}

bool ethernet_link_insert(ethernet_link_t *link, machine_t *machine) {
    if (!net_gateway_available() || !open_gateway(link)) return false;
    if (machine_insert_ethernet(machine, ETHERNET_LINK_MAC)) return true;
    ethernet_link_close(link);
    return false;
}

void ethernet_link_pump(ethernet_link_t *link, machine_t *machine) {
    if (!machine_ethernet_inserted(machine)) {
        ethernet_link_close(link);
        return;
    }
    if (!open_gateway(link)) return;
    uint8_t frame[NE2000_FRAME_MAX];
    size_t length;
    while ((length = machine_ethernet_take(machine, frame, sizeof frame)) > 0) ethernet_gateway_from_guest(link->gateway, frame, length);
    ethernet_gateway_poll(link->gateway, machine_cycles(machine) / (MACHINE_CLOCK_HZ / 1000));
    while ((length = ethernet_gateway_to_guest(link->gateway, frame, sizeof frame)) > 0) machine_ethernet_give(machine, frame, length);
}

void ethernet_link_close(ethernet_link_t *link) {
    if (!link->gateway) return;
    ethernet_gateway_destroy(link->gateway);
    link->gateway = NULL;
}
