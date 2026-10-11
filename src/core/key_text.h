#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KEY_TEXT_SHIFT 0x53

typedef enum { KEY_LAYOUT_US } key_layout_t;

bool key_text_find(key_layout_t layout, char character, uint8_t *scancode, bool *shifted);
