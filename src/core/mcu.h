#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MCU_TX_DUMMY   0x11
#define MCU_ERROR_CODE 0x00

typedef struct {
    bool selected;
    uint8_t command;
    int phase;
    uint8_t reply;
    uint8_t queue[32];
    int queue_length;
    int queue_position;
    uint8_t keys[64];
    int key_count;
    bool pen_down;
    uint16_t pen_x, pen_y;
    uint16_t main_battery, backup_battery;
    uint8_t contrast, brightness;
    bool contrast_on, brightness_on;
    uint8_t last_unknown;
} mcu_t;

void mcu_reset(mcu_t *mcu);
void mcu_select(mcu_t *mcu, bool selected);
uint8_t mcu_exchange(mcu_t *mcu, uint8_t value);
bool mcu_key(mcu_t *mcu, uint8_t scancode, bool down);
bool mcu_keyboard_pending(const mcu_t *mcu);
void mcu_pen(mcu_t *mcu, bool down, uint16_t x, uint16_t y);
