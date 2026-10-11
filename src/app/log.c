#include "app/log.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static bool verbose;

void app_log_set_verbose(bool enabled) {
    verbose = enabled;
}

bool app_log_verbose(void) {
    return verbose;
}

void app_log(const char *message) {
#ifdef __ANDROID__
    SDL_Log("%s", message);
#endif
    if (verbose) fputs(message, stderr);
}

void app_log_always(const char *message) {
#ifdef __ANDROID__
    SDL_Log("%s", message);
#endif
    fputs(message, stderr);
}
