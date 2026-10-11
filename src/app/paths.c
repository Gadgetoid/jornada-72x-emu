#include "app/paths.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

#include "rapi/rapi.h"

void app_data_folder(char *path, size_t size) {
    rapi_data_path("", path, size);
    size_t length = strlen(path);
    if (length > 1 && path[length - 1] == '/') path[length - 1] = 0;
    SDL_CreateDirectory(path);
}

void app_state_path(char *path, size_t size) {
    char folder[1024];
    app_data_folder(folder, sizeof folder);
    snprintf(path, size, "%s/state.bin", folder);
}

bool app_rapi_socket_path(char *path, size_t size) {
    char folder[1024];
    app_data_folder(folder, sizeof folder);
    return rapi_data_path("rapi.sock", path, size);
}
