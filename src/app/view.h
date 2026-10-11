#pragma once

#include <SDL3/SDL.h>

#include "core/sed1356.h"

typedef struct view view_t;

view_t *view_create(SDL_Renderer *renderer);
void    view_destroy(view_t *view);
void    view_render(view_t *view, const sed1356_t *display);
