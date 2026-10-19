#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ROCKWELL_MDP_REGISTERS 32
#define ROCKWELL_MDP_RAM_WORDS 0x1000

typedef struct {
    uint8_t registers[ROCKWELL_MDP_REGISTERS];
    uint16_t ram[2][ROCKWELL_MDP_RAM_WORDS];
    uint8_t download_phase;
    uint8_t download_check;
} rockwell_mdp_t;

void    rockwell_mdp_reset(rockwell_mdp_t *pump);
uint8_t rockwell_mdp_read(const rockwell_mdp_t *pump, uint32_t reg);
void    rockwell_mdp_write(rockwell_mdp_t *pump, uint32_t reg, uint8_t value);
