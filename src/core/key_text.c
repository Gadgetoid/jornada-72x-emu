#include "core/key_text.h"

#include <stddef.h>

typedef struct { char plain, shifted; uint8_t scancode; } key_char_t;

static const key_char_t key_chars[] = {
    { 'a', 'A', 0x31 }, { 'b', 'B', 0x45 }, { 'c', 'C', 0x43 }, { 'd', 'D', 0x33 }, { 'e', 'E', 0x23 },
    { 'f', 'F', 0x34 }, { 'g', 'G', 0x35 }, { 'h', 'H', 0x36 }, { 'i', 'I', 0x28 }, { 'j', 'J', 0x37 },
    { 'k', 'K', 0x38 }, { 'l', 'L', 0x39 }, { 'm', 'M', 0x47 }, { 'n', 'N', 0x46 }, { 'o', 'O', 0x29 },
    { 'p', 'P', 0x2A }, { 'q', 'Q', 0x21 }, { 'r', 'R', 0x24 }, { 's', 'S', 0x32 }, { 't', 'T', 0x25 },
    { 'u', 'U', 0x27 }, { 'v', 'V', 0x44 }, { 'w', 'W', 0x22 }, { 'x', 'X', 0x42 }, { 'y', 'Y', 0x26 },
    { 'z', 'Z', 0x41 },
    { '1', '!', 0x11 }, { '2', '@', 0x12 }, { '3', '#', 0x13 }, { '4', '$', 0x14 }, { '5', '%', 0x15 },
    { '6', '^', 0x16 }, { '7', '&', 0x17 }, { '8', '*', 0x18 }, { '9', '(', 0x19 }, { '0', ')', 0x1A },
    { '-', '_', 0x1B }, { '=', '+', 0x1C }, { '\\', '|', 0x2B }, { ';', ':', 0x3A }, { '\'', '"', 0x4B },
    { ',', '<', 0x48 }, { '.', '>', 0x49 }, { '/', '?', 0x78 }, { ' ', 0, 0x74 }, { '\n', 0, 0x4C },
    { '\t', 0, 0x51 },
};

bool key_text_find(key_layout_t layout, char character, uint8_t *scancode, bool *shifted) {
    (void)layout;
    for (size_t i = 0; i < sizeof key_chars / sizeof key_chars[0]; i++) {
        bool is_shifted = key_chars[i].shifted && key_chars[i].shifted == character;
        if (key_chars[i].plain != character && !is_shifted) continue;
        *scancode = key_chars[i].scancode;
        *shifted = is_shifted;
        return true;
    }
    return false;
}
