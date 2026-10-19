#include "core/rockwell_mdp.h"

#include <string.h>

#define REG_TRANSMIT_STATUS 0x0D
#define REG_TRANSMIT_DATA   0x10
#define REG_CONFIGURATION   0x12
#define REG_COMMAND         0x14
#define REG_Y_DATA_LOW      0x16
#define REG_Y_DATA_HIGH     0x17
#define REG_Y_ADDRESS       0x1A
#define REG_Y_CONTROL       0x1B
#define REG_X_DATA_LOW      0x18
#define REG_X_ADDRESS       0x1C
#define REG_X_CONTROL       0x1D
#define REG_INTERRUPT       0x1F

#define CONFIGURATION_READY 0x76
#define TRANSMIT_EMPTY      0x02
#define DOWNLOAD_READY      0xFF
#define ACCESS_START        0x80
#define ACCESS_BANK         0x40
#define ACCESS_WRITE        0x20
#define ACCESS_ADDRESS_HIGH 0x0F
#define INTERRUPT_NEWC      0x01

enum { DOWNLOAD_IDLE, DOWNLOAD_ADDRESS, DOWNLOAD_DATA };

void rockwell_mdp_reset(rockwell_mdp_t *pump) {
    memset(pump, 0, sizeof *pump);
    pump->registers[REG_CONFIGURATION] = CONFIGURATION_READY;
}

uint8_t rockwell_mdp_read(const rockwell_mdp_t *pump, uint32_t reg) {
    return pump->registers[reg % ROCKWELL_MDP_REGISTERS];
}

static void ram_access(rockwell_mdp_t *pump, uint32_t control, uint32_t address_reg, uint32_t data_reg, uint8_t value) {
    uint32_t address = ((uint32_t)(value & ACCESS_ADDRESS_HIGH) << 8) | pump->registers[address_reg];
    uint16_t *word = &pump->ram[(value & ACCESS_BANK) ? 1 : 0][address];
    if (value & ACCESS_WRITE) {
        *word = (uint16_t)(pump->registers[data_reg] | pump->registers[data_reg + 1] << 8);
    } else {
        pump->registers[data_reg] = (uint8_t)*word;
        pump->registers[data_reg + 1] = (uint8_t)(*word >> 8);
    }
    pump->registers[control] = value & (uint8_t)~ACCESS_START;
}

static void command(rockwell_mdp_t *pump, uint8_t value) {
    if (value) {
        pump->registers[REG_Y_CONTROL] = value;
        pump->registers[REG_Y_DATA_LOW] = DOWNLOAD_READY;
        pump->download_phase = DOWNLOAD_ADDRESS;
        pump->download_check = 0;
        return;
    }
    pump->registers[REG_Y_DATA_LOW] = pump->download_check;
    pump->registers[REG_Y_DATA_HIGH] = DOWNLOAD_READY;
    pump->download_phase = DOWNLOAD_IDLE;
}

void rockwell_mdp_write(rockwell_mdp_t *pump, uint32_t reg, uint8_t value) {
    reg %= ROCKWELL_MDP_REGISTERS;
    pump->registers[reg] = value;
    switch (reg) {
    case REG_COMMAND:
        command(pump, value);
        break;
    case REG_Y_DATA_HIGH:
        if (pump->download_phase == DOWNLOAD_ADDRESS) {
            pump->download_phase = DOWNLOAD_DATA;
            pump->registers[REG_TRANSMIT_STATUS] |= TRANSMIT_EMPTY;
        }
        break;
    case REG_TRANSMIT_DATA:
        if (pump->download_phase == DOWNLOAD_DATA)
            pump->download_check = (uint8_t)(((pump->download_check << 1) | (pump->download_check >> 7)) + value);
        break;
    case REG_X_CONTROL:
        if (value & ACCESS_START) ram_access(pump, REG_X_CONTROL, REG_X_ADDRESS, REG_X_DATA_LOW, value);
        break;
    case REG_Y_CONTROL:
        if ((value & ACCESS_START) && pump->download_phase == DOWNLOAD_IDLE)
            ram_access(pump, REG_Y_CONTROL, REG_Y_ADDRESS, REG_Y_DATA_LOW, value);
        break;
    case REG_INTERRUPT:
        pump->registers[REG_INTERRUPT] = value & (uint8_t)~INTERRUPT_NEWC;
        break;
    default:
        break;
    }
}
