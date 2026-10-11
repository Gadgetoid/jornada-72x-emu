#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_IMAGE_MAX_WIDTH  436
#define WEB_IMAGE_MAX_HEIGHT 1200

bool web_image_convert(const uint8_t *data, size_t length, bool svg, int hint_width, int hint_height, uint8_t **gif, size_t *gif_length);
