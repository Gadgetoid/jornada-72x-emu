#include "app/paths.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rapi/rapi.h"
#include "rapi/rapi_project.h"
#include "util/socket_path.h"

#define SETTINGS_FILE "jornada720.ini"

void app_data_folder(char *path, size_t size) {
    rapi_data_path("", path, size);
    size_t length = strlen(path);
    if (length > 1 && path[length - 1] == '/') path[length - 1] = 0;
    SDL_CreateDirectory(path);
}

void app_settings_path(char *path, size_t size) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    char base[1024];
    if (config_home && config_home[0] == '/') snprintf(base, sizeof base, "%s/" RAPI_DATA_FOLDER, config_home);
#ifdef __APPLE__
    else app_data_folder(base, sizeof base);
#else
    else snprintf(base, sizeof base, "%s/.config/" RAPI_DATA_FOLDER, getenv("HOME") ? getenv("HOME") : ".");
#endif
    SDL_CreateDirectory(base);
    snprintf(path, size, "%s/" SETTINGS_FILE, base);
}

bool app_rapi_socket_path(char *path, size_t size) {
#ifdef __ANDROID__
    return socket_path(path, size, RAPI_TOOL);
#else
    return rapi_data_path("rapi.sock", path, size);
#endif
}
