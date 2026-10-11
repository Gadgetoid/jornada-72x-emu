#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SCREEN_STOCK_WIDTH  640
#define SCREEN_STOCK_HEIGHT 240

typedef struct {
    uint16_t width, height;
} screen_size_t;

extern const screen_size_t SCREEN_PRESETS[];
extern const int SCREEN_PRESET_COUNT;

int screen_preset_index(screen_size_t size);
