#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct view view_t;

view_t         *view_create(SDL_Window *window, SDL_Renderer *renderer, int top);
void            view_destroy(view_t *view);
void            view_set_insets(view_t *view, int left, int top, int right, int bottom);
void            view_window_size(uint32_t scale, int bar_height, int *width, int *height);
void            view_fit_window(view_t *view, uint32_t scale);
uint32_t       *view_frame(view_t *view);
bool            view_update(view_t *view);
void            view_render(view_t *view);
bool            view_screen_position(view_t *view, float window_x, float window_y, int *x, int *y);
const uint32_t *view_image(view_t *view, int *width, int *height);
