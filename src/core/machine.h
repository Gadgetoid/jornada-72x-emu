#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "core/arm.h"
#include "core/eeprom.h"
#include "core/mailbox.h"
#include "core/mcu.h"
#include "core/optimiser.h"
#include "core/sa1110.h"
#include "core/sa1111.h"
#include "core/sed1356.h"

#define MACHINE_CPU_HZ   206400000u
#define MACHINE_ROM_SIZE 0x02000000u
#define MACHINE_DRAM_SIZE 0x02000000u

#define MACHINE_UNKNOWN_MAX 256
#define MACHINE_AUDIO_FRAMES 65536
#define MACHINE_SERIAL_QUEUE 8192
#define MACHINE_SERIAL_PORT  3
#define MACHINE_GPIO_SERIAL_DETECT 15

typedef struct {
    uint32_t pa;
    uint32_t pc;
    uint32_t value;
    uint32_t count;
    bool write;
    uint8_t size;
} machine_unknown_t;

typedef struct machine machine_t;

struct machine {
    arm_cpu_t cpu;
    optimiser_t optimiser;
    sa1110_t soc;
    sa1111_t sa1111;
    sed1356_t display;
    eeprom_t eeprom;
    mcu_t mcu;
    uint8_t *rom;
    uint8_t *dram;
    uint32_t dram_size;
    uint8_t zero[0x400];
    uint8_t open_bus[0x400];
    machine_unknown_t unknown[MACHINE_UNKNOWN_MAX];
    int unknown_count;
    FILE *log;
    bool powered_off;
    bool reset_requested;
    uint64_t events;
    uint64_t pen_next_sample;
    int16_t audio[MACHINE_AUDIO_FRAMES * 2];
    uint32_t audio_read, audio_write;
    uint32_t audio_rate;
    mailbox_t mailbox;
    uint32_t mailbox_fault_va;
    int mailbox_fault_tries;
    uint64_t mailbox_received_at;
    bool serial_connected;
    uint8_t serial_rx[MACHINE_SERIAL_QUEUE];
    uint32_t serial_rx_read, serial_rx_write;
    uint8_t serial_tx[MACHINE_SERIAL_QUEUE];
    uint32_t serial_tx_read, serial_tx_write;
};

bool machine_init(machine_t *machine, const char *rom_path, const char *eeprom_path);
void machine_set_optimisations(machine_t *machine, bool enabled);
void machine_free(machine_t *machine);
void machine_reset(machine_t *machine);
void machine_soft_reset(machine_t *machine);
void machine_run(machine_t *machine, uint64_t cycles);
void machine_key(machine_t *machine, uint8_t scancode, bool down);
void machine_pen(machine_t *machine, bool down, uint16_t x, uint16_t y);
void machine_touch(machine_t *machine, bool down, int x, int y);
uint32_t machine_audio_take(machine_t *machine, int16_t *frames, uint32_t max_frames, uint32_t *rate);
void machine_serial_connect(machine_t *machine, bool connected);
void machine_restore_serial_detect(machine_t *machine);
bool machine_serial_connected(const machine_t *machine);
void machine_serial_send(machine_t *machine, const uint8_t *data, size_t length);
size_t machine_serial_take(machine_t *machine, uint8_t *out, size_t max);
size_t machine_serial_space(const machine_t *machine);
uint32_t machine_serial_baud(const machine_t *machine);
bool machine_serial_dtr(const machine_t *machine);
uint64_t machine_cycles(const machine_t *machine);
bool machine_agent_running(const machine_t *machine);
void machine_print_status(const machine_t *machine, FILE *out);
double machine_seconds(const machine_t *machine);
