#pragma once

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>

#include "app/desktop.h"

typedef enum { PICK_SAVE_SNAPSHOT = 1, PICK_LOAD_SNAPSHOT, PICK_SEND, PICK_FETCH, PICK_SHARED } pick_kind_t;

#define PICK_MAX 64

typedef struct {
    pick_kind_t kind;
    int count;
    char paths[PICK_MAX][1024];
    char export_uri[1024];
} picked_t;

typedef struct {
    char paths[PICK_MAX][1024];
    int count;
} dropped_t;

void      picks_init(void);
void      picks_done(void *userdata, const char *const *files, int filter);
picked_t *picks_take(const SDL_Event *event);
void      picks_handle_drop(dropped_t *dropped, desktop_t *desktop, bool online, char *message, size_t size);
