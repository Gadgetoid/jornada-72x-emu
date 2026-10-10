#include "core/mcu.h"

#include <string.h>

#define COMMAND_GET_CONTRAST     0xD0
#define COMMAND_SET_CONTRAST     0xD1
#define COMMAND_GET_BRIGHTNESS   0xD2
#define COMMAND_SET_BRIGHTNESS   0xD3
#define COMMAND_CONTRAST_OFF     0xD8
#define COMMAND_BRIGHTNESS_OFF   0xD9
#define COMMAND_PWM_OFF          0xDF
#define COMMAND_GET_SCAN_KEY     0x90
#define COMMAND_GET_BATTERY      0xC0
#define COMMAND_GET_TOUCH        0xA0

void mcu_reset(mcu_t *mcu) {
    memset(mcu, 0, sizeof *mcu);
    mcu->main_battery = 0x300;
    mcu->backup_battery = 0x3C0;
    mcu->contrast = 0x19;
    mcu->brightness = 0x80;
    mcu->contrast_on = mcu->brightness_on = true;
}

void mcu_select(mcu_t *mcu, bool selected) {
    if (selected && !mcu->selected) {
        mcu->phase = 0;
        mcu->queue_length = mcu->queue_position = 0;
        mcu->reply = MCU_TX_DUMMY;
    }
    mcu->selected = selected;
}

static void queue(mcu_t *mcu, uint8_t value) {
    if (mcu->queue_length < (int)sizeof mcu->queue) mcu->queue[mcu->queue_length++] = value;
}

static void start_command(mcu_t *mcu, uint8_t command) {
    mcu->command = command;
    mcu->queue_length = mcu->queue_position = 0;
    switch (command) {
    case COMMAND_GET_SCAN_KEY:
        queue(mcu, (uint8_t)mcu->key_count);
        for (int i = 0; i < mcu->key_count; i++) queue(mcu, mcu->keys[i]);
        mcu->key_count = 0;
        break;
    case COMMAND_GET_TOUCH:
        for (int i = 0; i < 3; i++) queue(mcu, mcu->pen_x & 0xFF);
        for (int i = 0; i < 3; i++) queue(mcu, mcu->pen_y & 0xFF);
        queue(mcu, (uint8_t)(((mcu->pen_x >> 8) & 3) * 0x15));
        queue(mcu, (uint8_t)(((mcu->pen_y >> 8) & 3) * 0x15));
        break;
    case COMMAND_GET_BATTERY:
        queue(mcu, mcu->main_battery & 0xFF);
        queue(mcu, mcu->backup_battery & 0xFF);
        queue(mcu, (uint8_t)(((mcu->main_battery >> 8) & 3) | (((mcu->backup_battery >> 8) & 3) << 2)));
        break;
    case COMMAND_GET_CONTRAST:
        queue(mcu, mcu->contrast);
        break;
    case COMMAND_GET_BRIGHTNESS:
        queue(mcu, mcu->brightness);
        break;
    case COMMAND_CONTRAST_OFF:
        mcu->contrast_on = false;
        break;
    case COMMAND_BRIGHTNESS_OFF:
        mcu->brightness_on = false;
        break;
    case COMMAND_PWM_OFF:
        mcu->contrast_on = mcu->brightness_on = false;
        break;
    case COMMAND_SET_CONTRAST:
    case COMMAND_SET_BRIGHTNESS:
        break;
    default:
        mcu->last_unknown = command;
        break;
    }
}

uint8_t mcu_exchange(mcu_t *mcu, uint8_t value) {
    if (!mcu->selected) return 0xFF;
    uint8_t reply = mcu->reply;
    if (mcu->phase++ == 0 || (value != MCU_TX_DUMMY && mcu->queue_position >= mcu->queue_length
                              && mcu->command != COMMAND_SET_CONTRAST && mcu->command != COMMAND_SET_BRIGHTNESS)) {
        start_command(mcu, value);
        mcu->phase = 1;
        mcu->reply = mcu->queue_length ? mcu->queue[mcu->queue_position++] : MCU_TX_DUMMY;
        return reply;
    }
    if (mcu->command == COMMAND_SET_CONTRAST) {
        mcu->contrast = value;
        mcu->contrast_on = true;
        mcu->command = 0;
    } else if (mcu->command == COMMAND_SET_BRIGHTNESS) {
        mcu->brightness = value;
        mcu->brightness_on = true;
        mcu->command = 0;
    }
    mcu->reply = mcu->queue_position < mcu->queue_length ? mcu->queue[mcu->queue_position++] : MCU_TX_DUMMY;
    return reply;
}

bool mcu_key(mcu_t *mcu, uint8_t scancode, bool down) {
    if (mcu->key_count == (int)sizeof mcu->keys) return false;
    mcu->keys[mcu->key_count++] = (uint8_t)((scancode & 0x7F) | (down ? 0 : 0x80));
    return true;
}

bool mcu_keyboard_pending(const mcu_t *mcu) {
    return mcu->key_count != 0;
}

void mcu_pen(mcu_t *mcu, bool down, uint16_t x, uint16_t y) {
    mcu->pen_down = down;
    mcu->pen_x = x & 0x3FF;
    mcu->pen_y = y & 0x3FF;
}
