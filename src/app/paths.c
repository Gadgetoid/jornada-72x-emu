#include "app/paths.h"

#include <SDL3/SDL.h>

#include <stdio.h>

void app_data_folder(char *path, size_t size) {
    char *preferences = SDL_GetPrefPath(NULL, "jornada-72x-emu");
    snprintf(path, size, "%s", preferences ? preferences : "");
    SDL_free(preferences);
}

void app_state_path(char *path, size_t size) {
    char folder[1024];
    app_data_folder(folder, sizeof folder);
    snprintf(path, size, "%sstate.bin", folder);
}
