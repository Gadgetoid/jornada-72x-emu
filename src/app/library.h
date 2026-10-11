#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app/rom_catalog.h"
#include "frontend/common/dialog.h"

void library_folder(const char *name, char *path, size_t size);
void library_find_roms(rom_set_t *roms);
void library_machines_folder(char *path, size_t size);
int  library_list_roms(dialog_rom_t *roms, int max);
void library_show_no_roms(void);
#ifdef __ANDROID__
int  library_import_files(int *cards);
bool library_first_run_import(void);
#endif
