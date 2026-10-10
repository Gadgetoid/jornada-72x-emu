#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SED1356_REGISTER_BASE 0x48000000u
#define SED1356_REGISTER_SIZE 0x00200000u
#define SED1356_MEMORY_BASE   0x48200000u
#define SED1356_MEMORY_SIZE   0x00080000u

typedef struct {
    uint8_t registers[0x200];
    uint8_t memory[SED1356_MEMORY_SIZE];
    uint8_t lut[256][3];
    uint8_t lut_pending[3];
    int lut_index;
    int lut_component;
    bool blit_active;
    int blit_start_reads;
    int blit_operation;
    int blit_x, blit_y;
    int blit_width, blit_height;
    uint32_t blit_words_left;
} sed1356_t;

void sed1356_reset(sed1356_t *display);
uint32_t sed1356_read(sed1356_t *display, uint32_t offset, int size);
void sed1356_write(sed1356_t *display, uint32_t offset, int size, uint32_t value);
bool sed1356_size(const sed1356_t *display, int *width, int *height);
bool sed1356_render(const sed1356_t *display, uint32_t *pixels, int width, int height);
