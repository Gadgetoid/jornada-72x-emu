#include "app/library.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app/host.h"
#include "app/paths.h"
#include "frontend/android/android.h"
#include "util/file.h"

#define IMPORT_MAX         64
#define CARD_MIN_BYTES     (1024 * 1024)
#define DEVELOPMENT_ROM    "rom/jornada720.bin"
#define DEVELOPMENT_EEPROM "rom/jorn720_eeprom.bin"

void library_folder(const char *name, char *path, size_t size) {
    char base[1024];
    app_data_folder(base, sizeof base);
    snprintf(path, size, "%s/%s", base, name);
    SDL_CreateDirectory(path);
}

static void rom_folder(char *path, size_t size) {
    library_folder("roms", path, size);
}

static void find_development_roms(rom_set_t *roms) {
#ifndef __ANDROID__
    char path[PATH_MAX];
    if (!rom_catalog_probe(DEVELOPMENT_ROM) || !realpath(DEVELOPMENT_ROM, path) || strlen(path) >= sizeof roms->rom) return;
    snprintf(roms->rom, sizeof roms->rom, "%s", path);
    if (!roms->eeprom[0] && rom_catalog_eeprom(DEVELOPMENT_EEPROM) && realpath(DEVELOPMENT_EEPROM, path) && strlen(path) < sizeof roms->eeprom) snprintf(roms->eeprom, sizeof roms->eeprom, "%s", path);
#else
    (void)roms;
#endif
}

void library_find_roms(rom_set_t *roms) {
    char folder[1100];
    rom_folder(folder, sizeof folder);
    rom_catalog_find(roms, folder);
    if (!roms->rom[0]) find_development_roms(roms);
}

void library_show_no_roms(void) {
    char folder[1100], message[1400];
    rom_folder(folder, sizeof folder);
    snprintf(message, sizeof message, "Put a Jornada 720 ROM image in %s, with its configuration EEPROM if you have one.", folder);
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Show ROM Folder" },
    };
    const SDL_MessageBoxData dialog = { SDL_MESSAGEBOX_INFORMATION, NULL, "No ROM found", message, 2, buttons, NULL };
    int chosen = 0;
    if (SDL_ShowMessageBox(&dialog, &chosen) && chosen == 1) host_open_path(folder);
}

#ifdef __ANDROID__
typedef struct {
    SDL_AtomicInt done;
    int count;
    char uris[IMPORT_MAX][1024];
} import_pick_t;

static void import_picked(void *userdata, const char *const *files, int filter) {
    (void)filter;
    import_pick_t *pick = userdata;
    pick->count = 0;
    while (files && files[pick->count] && pick->count < IMPORT_MAX) {
        snprintf(pick->uris[pick->count], sizeof pick->uris[0], "%s", files[pick->count]);
        pick->count++;
    }
    SDL_SetAtomicInt(&pick->done, 1);
}

int library_import_files(int *cards) {
    *cards = 0;
    import_pick_t *pick = calloc(1, sizeof *pick);
    if (!pick) return 0;
    SDL_ShowOpenFileDialog(import_picked, pick, NULL, NULL, 0, NULL, true);
    while (!SDL_GetAtomicInt(&pick->done)) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 100) && event.type == SDL_EVENT_QUIT) SDL_PushEvent(&event);
    }
    char roms[1100], card_folder[1100];
    rom_folder(roms, sizeof roms);
    library_folder("cards", card_folder, sizeof card_folder);
    int rom_count = 0;
    for (int i = 0; i < pick->count; i++) {
        char path[1200];
        if (!android_import(pick->uris[i], roms, path, sizeof path)) continue;
        if (rom_catalog_probe(path)) {
            rom_count++;
            continue;
        }
        if (rom_catalog_eeprom(path)) continue;
        struct stat info;
        char card[1200];
        snprintf(card, sizeof card, "%s/%s", card_folder, file_leaf_name(path));
        if (stat(path, &info) == 0 && info.st_size >= CARD_MIN_BYTES && info.st_size % 512 == 0 && rename(path, card) == 0) (*cards)++;
        else remove(path);
    }
    free(pick);
    return rom_count;
}

bool library_first_run_import(void) {
    SDL_Init(SDL_INIT_VIDEO);
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose Files" },
    };
    const SDL_MessageBoxData dialog = { SDL_MESSAGEBOX_INFORMATION, NULL, "Import ROM and Cards",
                                        "Choose a Jornada 720 ROM image, and its configuration EEPROM if you have one. Card images can be chosen at the same time.",
                                        2, buttons, NULL };
    int chosen = 0;
    if (!SDL_ShowMessageBox(&dialog, &chosen) || chosen != 1) return false;
    int cards;
    library_import_files(&cards);
    return true;
}
#endif

void library_machines_folder(char *path, size_t size) {
    library_folder("machines", path, size);
}

int library_list_roms(dialog_rom_t *roms, int max) {
    char folder[1100];
    rom_folder(folder, sizeof folder);
    int count = 0;
    DIR *dir = opendir(folder);
    struct dirent *entry;
    while (dir && (entry = readdir(dir)) && count < max) {
        if (entry->d_name[0] == '.') continue;
        dialog_rom_t *rom = &roms[count];
        if (snprintf(rom->path, sizeof rom->path, "%s/%s", folder, entry->d_name) >= (int)sizeof rom->path) continue;
        rom->screens = rom_catalog_label(rom->path, rom->label, sizeof rom->label, &rom->memory_min, &rom->memory_max);
        if (rom->screens) count++;
    }
    if (dir) closedir(dir);
    rom_set_t found;
    library_find_roms(&found);
    bool listed = false;
    for (int i = 0; i < count; i++) listed = listed || !strcmp(roms[i].path, found.rom);
    if (found.rom[0] && !listed && count < max) {
        dialog_rom_t *rom = &roms[count];
        snprintf(rom->path, sizeof rom->path, "%s", found.rom);
        rom->screens = rom_catalog_label(rom->path, rom->label, sizeof rom->label, &rom->memory_min, &rom->memory_max);
        if (rom->screens) count++;
    }
    return count;
}
