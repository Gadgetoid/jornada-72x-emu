#include "app/serial_service.h"

#include <string.h>

#include "app/paths.h"

void serial_service_init(serial_service_t *service, serial_mode_t mode, int tcp_port, net_gateway_log_fn log) {
    memset(service, 0, sizeof *service);
    service->mode = mode;
    serial_link_init(&service->link, log);
    service->link.tcp_port = tcp_port;
    if (app_rapi_socket_path(service->rapi_socket, sizeof service->rapi_socket)) service->link.options.rapi_socket = service->rapi_socket;
}

const char *serial_service_start(serial_service_t *service, machine_t *machine) {
    if (service->mode == SERIAL_OFF) return NULL;
    const char *failure = serial_link_open(&service->link, service->mode, NULL);
    if (failure) return failure;
    if (service->mode == SERIAL_NETWORK) machine_serial_connect(machine, true);
    return NULL;
}

bool serial_service_online(const serial_service_t *service) {
    return service->link.gateway && ppp_gateway_online(service->link.gateway);
}

bool serial_service_take_desktop(serial_service_t *service) {
    return service->link.gateway && net_gateway_take_desktop_connected(ppp_gateway_network(service->link.gateway));
}

void serial_service_step(serial_service_t *service, machine_t *machine, notice_t *notice) {
    serial_link_t *link = &service->link;
    if (link->mode == SERIAL_OFF) return;
    serial_link_pump(link, machine);
    if (link->mode == SERIAL_TCP && serial_link_attached(link) != machine_serial_connected(machine)) {
        bool attached = serial_link_attached(link);
        machine_serial_connect(machine, attached);
        if (attached != service->tcp_attached) notice_show(notice, attached ? "TCP client connected" : "TCP client disconnected", NOTICE_SHORT);
        service->tcp_attached = attached;
    }
}

void serial_service_close(serial_service_t *service) {
    serial_link_close(&service->link);
}
