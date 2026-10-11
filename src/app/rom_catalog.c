#include "app/rom_catalog.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "core/machine.h"
#include "util/file.h"

#define MEMORY_MEGABYTES (MACHINE_DRAM_SIZE / (1024 * 1024))

static bool regular_file(const char *path, struct stat *info) {
    return stat(path, info) == 0 && S_ISREG(info->st_mode);
}

bool rom_catalog_probe(const char *path) {
    struct stat info;
    if (!regular_file(path, &info)) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[MACHINE_ROM_HEADER_SIZE];
    size_t length = fread(header, 1, sizeof header, file);
    fclose(file);
    return machine_rom_supported(header, length, (size_t)info.st_size);
}

bool rom_catalog_eeprom(const char *path) {
    struct stat info;
    return regular_file(path, &info) && machine_eeprom_supported((size_t)info.st_size);
}

void rom_catalog_find(rom_set_t *roms, const char *folder) {
    memset(roms, 0, sizeof *roms);
    DIR *dir = opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[sizeof roms->rom];
        if (snprintf(path, sizeof path, "%s/%s", folder, entry->d_name) >= (int)sizeof path) continue;
        struct stat info;
        if (!regular_file(path, &info)) continue;
        if (!roms->eeprom[0] && rom_catalog_eeprom(path)) memcpy(roms->eeprom, path, sizeof path);
        else if ((size_t)info.st_size > roms->rom_size && rom_catalog_probe(path)) {
            memcpy(roms->rom, path, sizeof path);
            roms->rom_size = (size_t)info.st_size;
        }
    }
    closedir(dir);
}

uint32_t rom_catalog_label(const char *path, char *label, size_t label_size, uint32_t *memory_min, uint32_t *memory_max) {
    if (!rom_catalog_probe(path)) return 0;
    *memory_min = *memory_max = MEMORY_MEGABYTES;
    snprintf(label, label_size, "Jornada 720: %s", file_leaf_name(path));
    return 1;
}
