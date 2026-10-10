#include "core/sed1356.h"

#include <string.h>

#define REG_REVISION       0x000
#define REG_MISC           0x001
#define REG_LCD_WIDTH      0x032
#define REG_LCD_HEIGHT0    0x038
#define REG_LCD_HEIGHT1    0x039
#define REG_LCD_MODE       0x040
#define REG_LCD_START0     0x042
#define REG_LCD_OFFSET0    0x046
#define REG_BITBLT_CONTROL0 0x100
#define REG_BITBLT_CONTROL1 0x101
#define REG_BITBLT_ROP     0x102
#define REG_BITBLT_OPERATION 0x103
#define REG_BITBLT_SOURCE  0x104
#define REG_BITBLT_DESTINATION 0x108
#define REG_BITBLT_OFFSET  0x10C
#define REG_BITBLT_WIDTH   0x110
#define REG_BITBLT_HEIGHT  0x112
#define REG_BITBLT_BACKGROUND 0x114
#define REG_BITBLT_FOREGROUND 0x118
#define BLIT_APERTURE      0x100000u
#define REG_LUT_MODE       0x1E0
#define REG_LUT_ADDRESS    0x1E2
#define REG_LUT_DATA       0x1E4
#define REG_POWER_STATUS   0x1F1
#define REG_DISPLAY_MODE   0x1FC

#define REVISION       0x11u
#define MISC_REGISTERS_ONLY 0x80u
#define LCD_BLANK      0x80u

#define BLIT_ACTIVE       0x80u
#define BLIT_NOT_EMPTY    0x40u
#define BLIT_HALF_FULL    0x20u
#define BLIT_FULL         0x10u
#define BLIT_DESTINATION_LINEAR 0x02u
#define BLIT_SOURCE_LINEAR 0x01u

enum {
    BLIT_WRITE,
    BLIT_READ,
    BLIT_MOVE_POSITIVE,
    BLIT_MOVE_NEGATIVE,
    BLIT_TRANSPARENT_WRITE,
    BLIT_TRANSPARENT_MOVE,
    BLIT_PATTERN,
    BLIT_TRANSPARENT_PATTERN,
    BLIT_EXPAND,
    BLIT_TRANSPARENT_EXPAND,
    BLIT_MOVE_EXPAND,
    BLIT_TRANSPARENT_MOVE_EXPAND,
    BLIT_SOLID_FILL,
};

static uint32_t register_value(const sed1356_t *display, uint32_t index, int bytes) {
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) value |= (uint32_t)display->registers[index + (uint32_t)i] << (8 * i);
    return value;
}

static bool blit_wide(const sed1356_t *display) {
    return display->registers[REG_BITBLT_CONTROL1] & 1;
}

static uint32_t blit_source(const sed1356_t *display) {
    return register_value(display, REG_BITBLT_SOURCE, 3) & 0x1FFFFFu;
}

static uint32_t blit_destination(const sed1356_t *display) {
    return register_value(display, REG_BITBLT_DESTINATION, 3) & 0x1FFFFFu;
}

static uint32_t blit_stride(const sed1356_t *display) {
    return (register_value(display, REG_BITBLT_OFFSET, 2) & 0x7FFu) * 2;
}

static uint32_t line_address(const sed1356_t *display, uint32_t base, int y, bool linear, int width) {
    int bytes = blit_wide(display) ? 2 : 1;
    uint32_t stride = linear ? (uint32_t)(width * bytes) : blit_stride(display);
    return base + (uint32_t)y * stride;
}

static uint32_t pixel_read(const sed1356_t *display, uint32_t address) {
    address %= SED1356_MEMORY_SIZE;
    if (!blit_wide(display)) return display->memory[address];
    return display->memory[address] | (uint32_t)display->memory[(address + 1) % SED1356_MEMORY_SIZE] << 8;
}

static void pixel_write(sed1356_t *display, uint32_t address, uint32_t value) {
    address %= SED1356_MEMORY_SIZE;
    display->memory[address] = (uint8_t)value;
    if (blit_wide(display)) display->memory[(address + 1) % SED1356_MEMORY_SIZE] = (uint8_t)(value >> 8);
}

static uint32_t raster(uint32_t code, uint32_t source, uint32_t destination) {
    uint32_t result = 0;
    if (code & 1) result |= ~source & ~destination;
    if (code & 2) result |= ~source & destination;
    if (code & 4) result |= source & ~destination;
    if (code & 8) result |= source & destination;
    return result;
}

static uint32_t destination_pixel(const sed1356_t *display, int x, int y) {
    bool linear = display->registers[REG_BITBLT_CONTROL0] & BLIT_DESTINATION_LINEAR;
    int bytes = blit_wide(display) ? 2 : 1;
    return line_address(display, blit_destination(display), y, linear, display->blit_width) + (uint32_t)(x * bytes);
}

static void put_pixel(sed1356_t *display, int x, int y, uint32_t source, bool use_raster) {
    uint32_t address = destination_pixel(display, x, y);
    uint32_t value = source;
    if (use_raster) value = raster(display->registers[REG_BITBLT_ROP] & 15, source, pixel_read(display, address));
    pixel_write(display, address, value);
}

static uint32_t colour(const sed1356_t *display, uint32_t index) {
    return register_value(display, index, blit_wide(display) ? 2 : 1);
}

static void advance_pixel(sed1356_t *display) {
    if (++display->blit_x == display->blit_width) {
        display->blit_x = 0;
        display->blit_y++;
    }
}

static void finish_if_done(sed1356_t *display) {
    if (!display->blit_words_left) display->blit_active = false;
}

static void move_blit(sed1356_t *display, bool negative, bool transparent) {
    bool source_linear = display->registers[REG_BITBLT_CONTROL0] & BLIT_SOURCE_LINEAR;
    bool destination_linear = display->registers[REG_BITBLT_CONTROL0] & BLIT_DESTINATION_LINEAR;
    int bytes = blit_wide(display) ? 2 : 1;
    int width = display->blit_width, height = display->blit_height;
    uint32_t key = colour(display, REG_BITBLT_BACKGROUND);
    uint32_t code = display->registers[REG_BITBLT_ROP] & 15;
    uint32_t source_stride = source_linear ? (uint32_t)(width * bytes) : blit_stride(display);
    uint32_t destination_stride = destination_linear ? (uint32_t)(width * bytes) : blit_stride(display);
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            uint32_t source, destination;
            if (negative) {
                source = blit_source(display) - (uint32_t)row * source_stride - (uint32_t)(column * bytes);
                destination = blit_destination(display) - (uint32_t)row * destination_stride - (uint32_t)(column * bytes);
            } else {
                source = blit_source(display) + (uint32_t)row * source_stride + (uint32_t)(column * bytes);
                destination = blit_destination(display) + (uint32_t)row * destination_stride + (uint32_t)(column * bytes);
            }
            uint32_t value = pixel_read(display, source);
            if (transparent) {
                if (value != key) pixel_write(display, destination, value);
            } else {
                pixel_write(display, destination, raster(code, value, pixel_read(display, destination)));
            }
        }
    }
}

static void pattern_blit(sed1356_t *display, bool transparent) {
    bool wide = blit_wide(display);
    uint32_t source = blit_source(display);
    uint32_t base = source & ~(wide ? 127u : 63u);
    int start_x = wide ? (int)((source >> 1) & 7) : (int)(source & 7);
    int start_y = wide ? (int)((source >> 4) & 7) : (int)((source >> 3) & 7);
    int bytes = wide ? 2 : 1;
    uint32_t key = colour(display, REG_BITBLT_BACKGROUND);
    for (int y = 0; y < display->blit_height; y++) {
        for (int x = 0; x < display->blit_width; x++) {
            int px = (start_x + x) & 7, py = (start_y + y) & 7;
            uint32_t value = pixel_read(display, base + (uint32_t)((py * 8 + px) * bytes));
            if (transparent) {
                if (value != key) put_pixel(display, x, y, value, false);
            } else {
                put_pixel(display, x, y, value, true);
            }
        }
    }
}

static void move_expand_blit(sed1356_t *display, bool transparent) {
    bool source_linear = display->registers[REG_BITBLT_CONTROL0] & BLIT_SOURCE_LINEAR;
    uint32_t source = blit_source(display);
    int phase = (int)(source & 1);
    int start_bit = display->registers[REG_BITBLT_ROP] & 7;
    int bit_offset = phase * 8 + (7 - start_bit);
    int words_per_line = (bit_offset + display->blit_width + 15) / 16;
    uint32_t stride = source_linear ? (uint32_t)(words_per_line * 2) : blit_stride(display);
    uint32_t foreground = colour(display, REG_BITBLT_FOREGROUND), background = colour(display, REG_BITBLT_BACKGROUND);
    for (int y = 0; y < display->blit_height; y++) {
        uint32_t line = (source & ~1u) + (uint32_t)y * stride;
        for (int x = 0; x < display->blit_width; x++) {
            int position = bit_offset + x;
            uint8_t byte = display->memory[(line + (uint32_t)(position / 8)) % SED1356_MEMORY_SIZE];
            bool set = (byte >> (7 - (position & 7))) & 1;
            if (set) put_pixel(display, x, y, foreground, false);
            else if (!transparent) put_pixel(display, x, y, background, false);
        }
    }
}

static uint32_t words_per_line(const sed1356_t *display) {
    if (display->blit_operation == BLIT_EXPAND || display->blit_operation == BLIT_TRANSPARENT_EXPAND) {
        int bit_offset = (int)(blit_source(display) & 1) * 8 + (7 - (display->registers[REG_BITBLT_ROP] & 7));
        return (uint32_t)((bit_offset + display->blit_width + 15) / 16);
    }
    if (blit_wide(display)) return (uint32_t)display->blit_width;
    uint32_t phase = display->blit_operation == BLIT_READ ? blit_destination(display) & 1 : blit_source(display) & 1;
    return ((uint32_t)display->blit_width + 1 + phase) / 2;
}

static void blit_start(sed1356_t *display) {
    display->blit_operation = display->registers[REG_BITBLT_OPERATION] & 15;
    display->blit_width = (int)(register_value(display, REG_BITBLT_WIDTH, 2) & 0x3FF) + 1;
    display->blit_height = (int)(register_value(display, REG_BITBLT_HEIGHT, 2) & 0x3FF) + 1;
    display->blit_x = display->blit_y = 0;
    display->blit_start_reads = 1;
    display->blit_active = false;
    switch (display->blit_operation) {
    case BLIT_WRITE:
    case BLIT_TRANSPARENT_WRITE:
    case BLIT_EXPAND:
    case BLIT_TRANSPARENT_EXPAND:
    case BLIT_READ:
        display->blit_words_left = words_per_line(display) * (uint32_t)display->blit_height;
        display->blit_active = display->blit_words_left != 0;
        break;
    case BLIT_MOVE_POSITIVE:
        move_blit(display, false, false);
        break;
    case BLIT_MOVE_NEGATIVE:
        move_blit(display, true, false);
        break;
    case BLIT_TRANSPARENT_MOVE:
        move_blit(display, false, true);
        break;
    case BLIT_PATTERN:
        pattern_blit(display, false);
        break;
    case BLIT_TRANSPARENT_PATTERN:
        pattern_blit(display, true);
        break;
    case BLIT_MOVE_EXPAND:
        move_expand_blit(display, false);
        break;
    case BLIT_TRANSPARENT_MOVE_EXPAND:
        move_expand_blit(display, true);
        break;
    case BLIT_SOLID_FILL:
        for (int y = 0; y < display->blit_height; y++)
            for (int x = 0; x < display->blit_width; x++)
                put_pixel(display, x, y, colour(display, REG_BITBLT_FOREGROUND), false);
        break;
    default:
        break;
    }
}

static void blit_data_write(sed1356_t *display, uint16_t word) {
    if (!display->blit_active || display->blit_operation == BLIT_READ) return;
    int operation = display->blit_operation;
    bool transparent = operation == BLIT_TRANSPARENT_WRITE || operation == BLIT_TRANSPARENT_EXPAND;
    if (operation == BLIT_EXPAND || operation == BLIT_TRANSPARENT_EXPAND) {
        int first = 0;
        if (display->blit_x == 0) first = (int)(blit_source(display) & 1) * 8 + (7 - (display->registers[REG_BITBLT_ROP] & 7));
        uint32_t foreground = colour(display, REG_BITBLT_FOREGROUND), background = colour(display, REG_BITBLT_BACKGROUND);
        int row = display->blit_y;
        for (int position = first; position < 16 && display->blit_y == row && display->blit_y < display->blit_height; position++) {
            uint8_t byte = position < 8 ? (uint8_t)word : (uint8_t)(word >> 8);
            bool set = (byte >> (7 - (position & 7))) & 1;
            if (set) put_pixel(display, display->blit_x, display->blit_y, foreground, false);
            else if (!transparent) put_pixel(display, display->blit_x, display->blit_y, background, false);
            advance_pixel(display);
        }
    } else if (blit_wide(display)) {
        uint32_t key = colour(display, REG_BITBLT_BACKGROUND);
        if (!transparent || word != key) put_pixel(display, display->blit_x, display->blit_y, word, !transparent);
        advance_pixel(display);
    } else {
        uint32_t key = colour(display, REG_BITBLT_BACKGROUND);
        int row = display->blit_y;
        int first = display->blit_x == 0 ? (int)(blit_source(display) & 1) : 0;
        for (int half = first; half < 2 && display->blit_y == row && display->blit_y < display->blit_height; half++) {
            uint32_t value = half ? word >> 8 : word & 0xFF;
            if (!transparent || value != key) put_pixel(display, display->blit_x, display->blit_y, value, !transparent);
            advance_pixel(display);
        }
    }
    display->blit_words_left--;
    finish_if_done(display);
}

static uint16_t blit_data_read(sed1356_t *display) {
    if (!display->blit_active || display->blit_operation != BLIT_READ) return 0;
    bool source_linear = display->registers[REG_BITBLT_CONTROL0] & BLIT_SOURCE_LINEAR;
    int bytes = blit_wide(display) ? 2 : 1;
    uint16_t word = 0;
    int row = display->blit_y;
    uint32_t line = line_address(display, blit_source(display), row, source_linear, display->blit_width);
    if (blit_wide(display)) {
        word = (uint16_t)pixel_read(display, line + (uint32_t)(display->blit_x * bytes));
        advance_pixel(display);
    } else {
        int first = display->blit_x == 0 ? (int)(blit_destination(display) & 1) : 0;
        for (int half = first; half < 2 && display->blit_y == row; half++) {
            word |= (uint16_t)(pixel_read(display, line + (uint32_t)display->blit_x) << (half * 8));
            advance_pixel(display);
        }
    }
    display->blit_words_left--;
    finish_if_done(display);
    return word;
}

static uint8_t blit_status(sed1356_t *display) {
    uint8_t status = display->registers[REG_BITBLT_CONTROL0] & 0x03;
    if (display->blit_active || display->blit_start_reads > 0) status |= BLIT_ACTIVE;
    if (display->blit_start_reads > 0) display->blit_start_reads--;
    if (display->blit_active && display->blit_operation == BLIT_READ) {
        uint32_t left = display->blit_words_left;
        status |= BLIT_NOT_EMPTY;
        if (left >= 7) status |= BLIT_HALF_FULL;
        if (left >= 15) status |= BLIT_FULL;
    }
    return status;
}

void sed1356_reset(sed1356_t *display) {
    memset(display->registers, 0, sizeof display->registers);
    display->registers[REG_MISC] = MISC_REGISTERS_ONLY;
    display->lut_index = display->lut_component = 0;
    display->blit_active = false;
    display->blit_start_reads = 0;
}

static uint8_t read_register(sed1356_t *display, uint32_t index) {
    if (index == REG_REVISION) return REVISION;
    if (display->registers[REG_MISC] & MISC_REGISTERS_ONLY && index != REG_MISC) return 0;
    switch (index) {
    case REG_BITBLT_CONTROL0:
        return blit_status(display);
    case REG_LUT_DATA: {
        uint8_t value = (uint8_t)(display->lut[display->lut_index][display->lut_component] << 4);
        if (++display->lut_component == 3) {
            display->lut_component = 0;
            display->lut_index = (display->lut_index + 1) & 0xFF;
        }
        return value;
    }
    case REG_POWER_STATUS:
        return 0;
    default:
        return display->registers[index];
    }
}

static void write_register(sed1356_t *display, uint32_t index, uint8_t value) {
    if (index == REG_REVISION) return;
    if (display->registers[REG_MISC] & MISC_REGISTERS_ONLY && index != REG_MISC) return;
    switch (index) {
    case REG_LUT_ADDRESS:
        display->lut_index = value;
        display->lut_component = 0;
        break;
    case REG_LUT_DATA:
        display->lut_pending[display->lut_component] = value >> 4;
        if (++display->lut_component == 3) {
            memcpy(display->lut[display->lut_index], display->lut_pending, 3);
            display->lut_component = 0;
            display->lut_index = (display->lut_index + 1) & 0xFF;
        }
        break;
    case REG_POWER_STATUS:
        return;
    case REG_BITBLT_CONTROL0:
        display->registers[index] = value & 0x03;
        if (value & BLIT_ACTIVE) blit_start(display);
        return;
    default:
        break;
    }
    display->registers[index] = value;
}

uint32_t sed1356_read(sed1356_t *display, uint32_t offset, int size) {
    if (offset >= BLIT_APERTURE) {
        uint32_t value = blit_data_read(display);
        if (size == 4) value |= (uint32_t)blit_data_read(display) << 16;
        return value;
    }
    uint32_t value = 0;
    for (int i = 0; i < size; i++) {
        uint32_t index = offset + (uint32_t)i;
        if (index < sizeof display->registers) value |= (uint32_t)read_register(display, index) << (8 * i);
    }
    return value;
}

void sed1356_write(sed1356_t *display, uint32_t offset, int size, uint32_t value) {
    if (offset >= BLIT_APERTURE) {
        blit_data_write(display, (uint16_t)value);
        if (size == 4) blit_data_write(display, (uint16_t)(value >> 16));
        return;
    }
    for (int i = 0; i < size; i++) {
        uint32_t index = offset + (uint32_t)i;
        if (index < sizeof display->registers) write_register(display, index, (uint8_t)(value >> (8 * i)));
    }
}

bool sed1356_size(const sed1356_t *display, int *width, int *height) {
    const uint8_t *r = display->registers;
    *width = (r[REG_LCD_WIDTH] + 1) * 8;
    *height = (r[REG_LCD_HEIGHT0] | ((r[REG_LCD_HEIGHT1] & 3) << 8)) + 1;
    return (r[REG_DISPLAY_MODE] & 7) != 0 && !(r[REG_LCD_MODE] & LCD_BLANK);
}

static uint32_t lut_colour(const sed1356_t *display, int index) {
    const uint8_t *entry = display->lut[index];
    return ((uint32_t)entry[0] * 0x11 << 16) | ((uint32_t)entry[1] * 0x11 << 8) | ((uint32_t)entry[2] * 0x11);
}

bool sed1356_render(const sed1356_t *display, uint32_t *pixels, int width, int height) {
    const uint8_t *r = display->registers;
    int panel_width, panel_height;
    bool active = sed1356_size(display, &panel_width, &panel_height);
    if (!active) {
        memset(pixels, 0, sizeof *pixels * (size_t)width * (size_t)height);
        return false;
    }
    uint32_t start = (r[REG_LCD_START0] | (r[REG_LCD_START0 + 1] << 8) | ((r[REG_LCD_START0 + 2] & 15) << 16)) * 2u;
    uint32_t stride = (r[REG_LCD_OFFSET0] | ((r[REG_LCD_OFFSET0 + 1] & 7) << 8)) * 2u;
    int bpp = r[REG_LCD_MODE] & 7;
    for (int y = 0; y < height; y++) {
        uint32_t line = start + (uint32_t)y * stride;
        for (int x = 0; x < width; x++) {
            uint32_t colour = 0;
            if (x < panel_width && y < panel_height) {
                if (bpp == 5 || bpp == 4) {
                    uint32_t at = (line + (uint32_t)x * 2) % SED1356_MEMORY_SIZE;
                    uint32_t word = display->memory[at] | (display->memory[at + 1] << 8);
                    uint32_t red, green, blue;
                    if (bpp == 5) {
                        red = (word >> 11) & 31;
                        green = (word >> 5) & 63;
                        blue = word & 31;
                        green = (green << 2) | (green >> 4);
                    } else {
                        red = (word >> 10) & 31;
                        green = (word >> 5) & 31;
                        blue = word & 31;
                        green = (green << 3) | (green >> 2);
                    }
                    colour = (((red << 3) | (red >> 2)) << 16) | (green << 8) | ((blue << 3) | (blue >> 2));
                } else if (bpp == 3) {
                    colour = lut_colour(display, display->memory[(line + (uint32_t)x) % SED1356_MEMORY_SIZE]);
                } else if (bpp == 2) {
                    uint8_t byte = display->memory[(line + (uint32_t)x / 2) % SED1356_MEMORY_SIZE];
                    colour = lut_colour(display, (x & 1) ? byte & 15 : byte >> 4);
                }
            }
            pixels[y * width + x] = colour;
        }
    }
    return true;
}
