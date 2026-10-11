#pragma once

#include <SDL3/SDL.h>

#include "core/machine.h"

#define PEN_QUEUE_SIZE 16

typedef struct {
    bool down;
    int x, y;
} pen_event_t;

typedef struct {
    pen_event_t events[PEN_QUEUE_SIZE];
    int head, count;
    bool down;
    double changed_at;
} pen_queue_t;

bool input_find_scancode(SDL_Scancode host, uint8_t *scancode);
void pen_queue_push(pen_queue_t *queue, bool down, int x, int y);
void pen_queue_run(pen_queue_t *queue, machine_t *machine);
