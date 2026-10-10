#pragma once
#include <stdbool.h>
#include <stdint.h>

#define EEPROM_SIZE 256

typedef struct {
    uint8_t data[EEPROM_SIZE];
    bool selected;
    bool write_enabled;
    int phase;
    uint8_t command;
    uint8_t address;
} eeprom_t;

void eeprom_select(eeprom_t *eeprom, bool selected);
uint8_t eeprom_exchange(eeprom_t *eeprom, uint8_t value);
