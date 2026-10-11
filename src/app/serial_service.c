#include "app/serial_service.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

#include "app/host.h"
#include "app/paths.h"
#include "net/ppp_gateway.h"

#define CABLE_REPLUG_SECONDS 2
#define CABLE_BOOT_SECONDS   20
#define CABLE_RESET_SECONDS  30
#define PORT_SCAN_MS         2000

static uint64_t cable_plug_time(machine_t *machine) {
    uint64_t replug = machine_cycles(machine) + CABLE_REPLUG_SECONDS * (uint64_t)MACHINE_CLOCK_HZ;
    uint64_t booted = CABLE_BOOT_SECONDS * (uint64_t)MACHINE_CLOCK_HZ;
    return replug > booted ? replug : booted;
}

static const char *set_serial(serial_service_t *service, machine_t *machine, serial_mode_t mode, const char *device) {
    serial_link_t *link = &service->link;
    machine_serial_connect(machine, false);
    service->plug_at = 0;
    bool same = link->mode == mode && (mode != SERIAL_DEVICE || !strcmp(link->name, device));
    if (!same) {
        const char *failure = serial_link_open(link, mode, device);
        if (failure) return failure;
        if (mode == SERIAL_PTY) fprintf(stderr, "serial: COM1 on %s\n", link->name);
    }
    if (mode != SERIAL_OFF && mode != SERIAL_TCP) service->plug_at = cable_plug_time(machine);
    return NULL;
}

void serial_service_init(serial_service_t *service, settings_t *settings) {
    memset(service, 0, sizeof *service);
    service->settings = settings;
    serial_link_init(&service->link, NULL);
    service->link.tcp_port = (int)settings->serial_tcp_port;
    service->link.options.user_agent = settings->user_agent;
    if (app_rapi_socket_path(service->rapi_socket, sizeof service->rapi_socket)) service->link.options.rapi_socket = service->rapi_socket;
    service->link.options.rapi_port = settings->network_rapi ? (int)settings->rapi_port : 0;
}

const char *serial_service_start(serial_service_t *service, machine_t *machine) {
    const char *failure = set_serial(service, machine, (serial_mode_t)service->settings->serial, service->settings->serial_device);
    if (failure) service->settings->serial = SERIAL_OFF;
    return failure;
}

const char *serial_service_select(serial_service_t *service, machine_t *machine, serial_mode_t mode, const char *device) {
    settings_t *settings = service->settings;
    const char *failure = set_serial(service, machine, mode, device);
    if (failure) return failure;
    settings->serial = mode;
    if (device) snprintf(settings->serial_device, sizeof settings->serial_device, "%s", device);
    settings_save(settings);
    if (mode == SERIAL_NETWORK) return "network cable plugged in; CE dials it";
    if (mode == SERIAL_OFF) return "serial cable unplugged";
    if (mode == SERIAL_TCP) {
        char address[64];
        host_local_address(address, sizeof address);
        snprintf(service->notice, sizeof service->notice, "COM1 at %s:%d; the cable connects while a client is attached", address, service->link.tcp_port);
        return service->notice;
    }
    snprintf(service->notice, sizeof service->notice, "COM1 on %s", service->link.name);
    return service->notice;
}

void serial_service_reattach(serial_service_t *service, machine_t *machine) {
    set_serial(service, machine, (serial_mode_t)service->settings->serial, service->settings->serial_device);
}

void serial_service_attach_machine(serial_service_t *service, machine_t *machine) {
    serial_service_reattach(service, machine);
    service->unplug_at = 0;
}

void serial_service_soft_reset(serial_service_t *service, machine_t *machine) {
    serial_service_reattach(service, machine);
    if (service->plug_at) service->plug_at = machine_cycles(machine) + CABLE_RESET_SECONDS * (uint64_t)MACHINE_CLOCK_HZ;
}

void serial_service_replug(serial_service_t *service, machine_t *machine, uint64_t seconds) {
    if (service->link.mode == SERIAL_NETWORK) service->unplug_at = machine_cycles(machine) + seconds * (uint64_t)MACHINE_CLOCK_HZ;
}

void serial_service_update_rapi(serial_service_t *service, machine_t *machine) {
    const settings_t *settings = service->settings;
    service->link.options.rapi_port = settings->network_rapi ? (int)settings->rapi_port : 0;
    if (service->link.mode != SERIAL_NETWORK) return;
    serial_link_close(&service->link);
    set_serial(service, machine, SERIAL_NETWORK, NULL);
}

bool serial_service_online(const serial_service_t *service) {
    return service->link.gateway && ppp_gateway_online(service->link.gateway);
}

void serial_service_step(serial_service_t *service, machine_t *machine, notice_t *notice) {
    serial_link_t *link = &service->link;
    if (service->unplug_at && machine_cycles(machine) >= service->unplug_at) {
        service->unplug_at = 0;
        if (link->mode == SERIAL_NETWORK) {
            serial_link_close(link);
            set_serial(service, machine, SERIAL_NETWORK, NULL);
        }
    }
    if (link->mode == SERIAL_TCP && serial_link_attached(link) != machine_serial_connected(machine)) {
        bool attached = serial_link_attached(link);
        machine_serial_connect(machine, attached);
        if (attached != service->tcp_attached) notice_show(notice, attached ? "TCP client connected" : "TCP client disconnected", NOTICE_SHORT);
        service->tcp_attached = attached;
    }
    if (service->plug_at && machine_cycles(machine) >= service->plug_at) {
        service->plug_at = 0;
        machine_serial_connect(machine, true);
    }
    if (SDL_GetTicks() >= service->port_scan_at) {
        service->port_scan_at = SDL_GetTicks() + PORT_SCAN_MS;
        service->port_count = serial_link_ports(service->ports, SERIAL_PORT_MAX);
    }
}

void serial_service_close(serial_service_t *service) {
    serial_link_close(&service->link);
}
