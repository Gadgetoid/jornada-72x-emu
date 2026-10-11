#include "core/screen.h"

const screen_size_t SCREEN_PRESETS[] = {
    { SCREEN_STOCK_WIDTH, SCREEN_STOCK_HEIGHT },
};
const int SCREEN_PRESET_COUNT = (int)(sizeof SCREEN_PRESETS / sizeof SCREEN_PRESETS[0]);

int screen_preset_index(screen_size_t size) {
    for (int i = 0; i < SCREEN_PRESET_COUNT; i++) {
        if (SCREEN_PRESETS[i].width == size.width && SCREEN_PRESETS[i].height == size.height) return i;
    }
    return -1;
}
