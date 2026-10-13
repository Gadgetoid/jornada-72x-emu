#include "app/ethernet_service.h"

#include "app/log.h"
#include "net/net_gateway.h"

void ethernet_service_init(ethernet_service_t *service, const settings_t *settings) {
    ethernet_link_init(&service->link, app_log, settings->user_agent);
}

bool ethernet_service_available(machine_t *machine) {
    return net_gateway_available() && (machine_ethernet_inserted(machine) || !machine_card_inserted(machine, MACHINE_SOCKET_PC_CARD));
}

const char *ethernet_service_toggle(ethernet_service_t *service, machine_t *machine) {
    if (machine_ethernet_inserted(machine)) {
        machine_eject_ethernet(machine);
        ethernet_link_close(&service->link);
        return "Ethernet card ejected";
    }
    if (ethernet_link_insert(&service->link, machine)) return "Ethernet card inserted; the first time, choose DHCP in CE's Socket LP-E settings";
    return "could not insert the Ethernet card";
}

void ethernet_service_close(ethernet_service_t *service) {
    ethernet_link_close(&service->link);
}
