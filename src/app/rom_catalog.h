#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char rom[1024];
    size_t rom_size;
    char eeprom[1024];
} rom_set_t;

void     rom_catalog_find(rom_set_t *roms, const char *folder);
bool     rom_catalog_probe(const char *path);
bool     rom_catalog_eeprom(const char *path);
uint32_t rom_catalog_label(const char *path, char *label, size_t label_size, uint32_t *memory_min, uint32_t *memory_max);
