#include "app/launch.h"

#include <stdio.h>
#include <string.h>

#include "core/machine.h"

enum {
    LAUNCH_HEADING_MACHINE, LAUNCH_MACHINE, LAUNCH_STATE, LAUNCH_FRESH, LAUNCH_CARD, LAUNCH_PC_CARD, LAUNCH_ETHERNET, LAUNCH_SPEED, LAUNCH_OPTIMISATIONS,
    LAUNCH_HEADING_CONNECTIONS, LAUNCH_NET, LAUNCH_TCP, LAUNCH_TCP_COM4, LAUNCH_USER_AGENT, LAUNCH_AGENT,
    LAUNCH_HEADING_DEBUGGING, LAUNCH_VERBOSE, LAUNCH_GDB, LAUNCH_GDB_PROCESS,
};

static const option_t LAUNCH_OPTIONS[] = {
    [LAUNCH_HEADING_MACHINE] = { NULL, NULL, "Machine", 0 },
    [LAUNCH_MACHINE] = { "machine", "NAME", "open the machine with this name (from Machine > Machines)", 0 },
    [LAUNCH_STATE] = { "state", "FILE", "load, save and autosave FILE instead of the machine's own state", 0 },
    [LAUNCH_FRESH] = { "fresh", NULL, "ignore the saved state and cold boot", 0 },
    [LAUNCH_CARD] = { "card", "IMAGE", "insert a CompactFlash card backed by a raw disk image", 0 },
    [LAUNCH_PC_CARD] = { "pc-card", "IMAGE", "insert a card image in the PC Card slot", 0 },
    [LAUNCH_ETHERNET] = { "ethernet", NULL, "insert an NE2000 Ethernet card in the PC Card slot, on its own network with DHCP", 0 },
    [LAUNCH_SPEED] = { "speed", "N", "CPU speed multiple against the timers: 1 or 2, and remember that", 0 },
    [LAUNCH_OPTIMISATIONS] = { "optimisations", "[on|off]", "run some of CE's routines natively and skip busy-waits on the clock, and remember that", 0 },
    [LAUNCH_HEADING_CONNECTIONS] = { NULL, NULL, "Connections", 0 },
    [LAUNCH_NET] = { "net", NULL, "plug the serial port into the PPP network (Devices > Serial Port), and remember that", 0 },
    [LAUNCH_TCP] = { "tcp", "[PORT]", "offer the serial port as raw bytes on a TCP port on all interfaces (9991, or PORT, kept in jornada720.ini), with the cable connected while a client is attached, and remember that", 0 },
    [LAUNCH_TCP_COM4] = { "tcp-com4", "PORT", "offer COM4, the raw IR port, as raw bytes on a TCP port on all interfaces, without IrDA framing; for this run only", 0 },
    [LAUNCH_USER_AGENT] = { "user-agent", "TEXT", "the web proxy's user agent, kept in jornada720.ini; empty passes Pocket IE's own through", 0 },
    [LAUNCH_AGENT] = { "agent", "SOCKET", "pass messages between a guest agent's swi 0x51CE mailbox and one client on this Unix socket", 0 },
    [LAUNCH_HEADING_DEBUGGING] = { NULL, NULL, "Debugging", 0 },
    [LAUNCH_VERBOSE] = { "verbose", NULL, "print the machine's status to stderr on quit", 0 },
    [LAUNCH_GDB] = { "gdb", "PORT", "listen for GDB on 127.0.0.1:PORT; it can attach and detach while the machine runs", 0 },
    [LAUNCH_GDB_PROCESS] = { "gdb-process", "NAME", "debug one process, e.g. hello.exe: breakpoints below 0x02000000 only stop there, and GDB stops when it starts", 0 },
};

static bool launch_option(void *context, int option, const char *value, char *error, size_t error_size) {
    launch_t *launch = context;
    settings_t *settings = launch->settings;
    long integer;
    (void)error;
    (void)error_size;
    switch (option) {
    case LAUNCH_MACHINE: launch->machine = value; return true;
    case LAUNCH_STATE: launch->state_file = value; return true;
    case LAUNCH_FRESH: launch->fresh = true; return true;
    case LAUNCH_CARD: launch->card = value; return true;
    case LAUNCH_PC_CARD: launch->pc_card = value; return true;
    case LAUNCH_ETHERNET: launch->ethernet = true; return true;
    case LAUNCH_SPEED:
        if (!option_integer(value, 10, &integer) || integer < 1 || integer > MACHINE_SPEED_MAX) return false;
        settings->speed = (uint32_t)integer;
        return true;
    case LAUNCH_OPTIMISATIONS:
        if (value && strcmp(value, "on") && strcmp(value, "off")) return false;
        settings->optimisations = !value || !strcmp(value, "on");
        return true;
    case LAUNCH_VERBOSE: launch->verbose = true; return true;
    case LAUNCH_GDB:
        if (!option_integer(value, 10, &integer) || integer < 1 || integer > 65535) return false;
        launch->gdb_port = (int)integer;
        return true;
    case LAUNCH_GDB_PROCESS: launch->gdb_process = value; return true;
    case LAUNCH_NET: settings->serial = SERIAL_NETWORK; return true;
    case LAUNCH_TCP:
        if (value && (!option_integer(value, 10, &integer) || integer < 1 || integer > 65535)) return false;
        if (value) settings->serial_tcp_port = (uint32_t)integer;
        settings->serial = SERIAL_TCP;
        return true;
    case LAUNCH_TCP_COM4:
        if (!option_integer(value, 10, &integer) || integer < 1 || integer > 65535) return false;
        launch->ir_tcp_port = (int)integer;
        return true;
    case LAUNCH_USER_AGENT: snprintf(settings->user_agent, sizeof settings->user_agent, "%s", value); return true;
    case LAUNCH_AGENT: launch->agent_socket = value; return true;
    }
    return false;
}

static const option_spec_t LAUNCH_SPEC = {
    "jornada720", "[OPTIONS] [ROM [EEPROM]]",
    "Emulates the HP Jornada 720 Handheld PC. With no ROM it opens the last machine used; machines are made with Machine > New Machine from the ROMs in the roms folder in its data folder. With a ROM, and optionally its configuration EEPROM, it runs that ROM with its own saved state, outside the machine list.",
    LAUNCH_OPTIONS, (int)(sizeof LAUNCH_OPTIONS / sizeof LAUNCH_OPTIONS[0]),
    "headless runs the machine without a window, for tests and scripts.",
};

options_result_t launch_parse(launch_t *launch, settings_t *settings, int argc, char **argv) {
    *launch = (launch_t){ .settings = settings };
    const char *positional[2];
    int positional_count;
    options_result_t parsed = options_parse(&LAUNCH_SPEC, argc, argv, launch_option, launch, positional, 2, &positional_count);
    if (parsed == OPTIONS_OK && positional_count > 0) launch->rom = positional[0];
    if (parsed == OPTIONS_OK && positional_count > 1) launch->eeprom = positional[1];
    return parsed;
}
