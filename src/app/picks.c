#include "app/picks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "frontend/android/android.h"
#include "util/file.h"

static Uint32 pick_event_type;

void picks_init(void) {
    pick_event_type = SDL_RegisterEvents(1);
}

void picks_done(void *userdata, const char *const *files, int filter) {
    (void)filter;
    if (!pick_event_type || !files || !files[0]) return;
    picked_t *picked = malloc(sizeof *picked);
    if (!picked) return;
    picked->kind = (pick_kind_t)(intptr_t)userdata;
    picked->count = 0;
    picked->export_uri[0] = 0;
    while (files[picked->count] && picked->count < PICK_MAX) {
        snprintf(picked->paths[picked->count], sizeof picked->paths[0], "%s", files[picked->count]);
        picked->count++;
    }
    SDL_Event event;
    SDL_zero(event);
    event.type = pick_event_type;
    event.user.data1 = picked;
    if (!SDL_PushEvent(&event)) free(picked);
}

static bool is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

void picks_handle_drop(dropped_t *dropped, desktop_t *desktop, bool online, char *message, size_t size) {
    int files = 0, scripts = -1;
    const char *list[PICK_MAX + 1];
    for (int i = 0; i < dropped->count; i++) {
        const char *path = dropped->paths[i];
        if (is_directory(path)) continue;
        if (file_has_extension(path, ".load") && scripts < 0) scripts = i;
        list[files++] = path;
    }
    list[files] = NULL;
    dropped->count = 0;
    if (!files) {
        snprintf(message, size, "drop files or a .load script");
    } else if (!online) {
        snprintf(message, size, "connect Devices > Serial Port > Network (PPP) to send files to the device");
    } else if (scripts >= 0) {
        if (desktop_load(desktop, dropped->paths[scripts])) snprintf(message, size, "installing %s", file_leaf_name(dropped->paths[scripts]));
        else snprintf(message, size, "busy with the last transfer");
    } else {
        snprintf(message, size, desktop_send(desktop, list) ? "sending to \\My Documents" : "busy with the last transfer");
    }
}

#ifdef __ANDROID__
static void localize_picked(picked_t *picked) {
    char folder[1100];
    snprintf(folder, sizeof folder, "%s", getenv("TMPDIR") ? getenv("TMPDIR") : ".");
    SDL_CreateDirectory(folder);
    for (int i = 0; i < picked->count; i++) {
        char uri[1024], path[1024];
        snprintf(uri, sizeof uri, "%s", picked->paths[i]);
        bool local;
        if (picked->kind == PICK_SAVE_SNAPSHOT) {
            local = android_local_path(uri, folder, path, sizeof path);
            snprintf(picked->export_uri, sizeof picked->export_uri, "%s", uri);
        } else {
            local = android_import(uri, folder, path, sizeof path);
        }
        snprintf(picked->paths[i], sizeof picked->paths[i], "%s", local ? path : "");
    }
}
#endif

picked_t *picks_take(const SDL_Event *event) {
    if (!pick_event_type || event->type != pick_event_type) return NULL;
    picked_t *picked = event->user.data1;
#ifdef __ANDROID__
    localize_picked(picked);
#endif
    return picked;
}
