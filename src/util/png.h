#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool png_encode(const uint32_t *pixels, int width, int height, uint8_t **png, size_t *png_length);
bool png_encode_rgba(const uint32_t *pixels, int width, int height, uint8_t **png, size_t *png_length);
