#include "core/eeprom.h"

#define COMMAND_WRITE_STATUS 0x01
#define COMMAND_WRITE        0x02
#define COMMAND_READ         0x03
#define COMMAND_WRITE_DISABLE 0x04
#define COMMAND_READ_STATUS  0x05
#define COMMAND_WRITE_ENABLE 0x06

void eeprom_select(eeprom_t *eeprom, bool selected) {
    if (selected && !eeprom->selected) eeprom->phase = 0;
    if (!selected && eeprom->selected && eeprom->command == COMMAND_WRITE && eeprom->phase > 2) eeprom->write_enabled = false;
    eeprom->selected = selected;
}

uint8_t eeprom_exchange(eeprom_t *eeprom, uint8_t value) {
    if (!eeprom->selected) return 0xFF;
    int phase = eeprom->phase++;
    if (phase == 0) {
        eeprom->command = value;
        if (value == COMMAND_WRITE_ENABLE) eeprom->write_enabled = true;
        if (value == COMMAND_WRITE_DISABLE) eeprom->write_enabled = false;
        return 0xFF;
    }
    switch (eeprom->command) {
    case COMMAND_READ:
        if (phase == 1) {
            eeprom->address = value;
            return 0xFF;
        }
        return eeprom->data[eeprom->address++];
    case COMMAND_WRITE:
        if (phase == 1) {
            eeprom->address = value;
            return 0xFF;
        }
        if (eeprom->write_enabled) eeprom->data[eeprom->address] = value;
        eeprom->address = (uint8_t)((eeprom->address & 0xF0) | ((eeprom->address + 1) & 0x0F));
        return 0xFF;
    case COMMAND_READ_STATUS:
        return eeprom->write_enabled ? 0x02 : 0x00;
    default:
        return 0xFF;
    }
}
