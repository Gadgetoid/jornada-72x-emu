#include "frontend/common/dialog.h"

#ifdef __APPLE__
#define CLOCK_SOURCE "this Mac"
#elif defined(__ANDROID__)
#define CLOCK_SOURCE "this phone"
#else
#define CLOCK_SOURCE "this computer"
#endif

const uint32_t DIALOG_MEMORY_SIZES[] = { 32 };

const int DIALOG_MEMORY_COUNT = (int)(sizeof DIALOG_MEMORY_SIZES / sizeof DIALOG_MEMORY_SIZES[0]);

const char *const DIALOG_MEMORY_LABELS[] = {
    "32 MB",
};

const char *const DIALOG_NEW_MACHINE_MESSAGE =
    "The clock setting is fixed for the life of the machine. A configuration EEPROM in the roms folder goes with it; without one the machine starts with a blank EEPROM.";

const char *const DIALOG_MANAGE_MESSAGE =
    "Reset sets a machine back to its factory state. Delete removes it and its saved state. Both put a backup in Snapshots/Backups first. The running machine can be reset but not deleted.";

const char *const DIALOG_CLOCK_LABEL = "Set the clock from " CLOCK_SOURCE " at the first boot";

const char *const DIALOG_ROM_PROMPT = "Choose a Jornada 720 ROM image.";

const char *const DIALOG_NOT_A_ROM = "Not a Jornada 720 ROM";

const char *const DIALOG_NO_ROM_HINT = "Put a Jornada 720 ROM in the roms folder, or choose Other ROM File" "\xe2\x80\xa6" ".";

static const char *const SCREEN_LABELS[] = { "640 x 240" };

const char *dialog_screen_label(int preset) {
    return preset >= 0 && preset < (int)(sizeof SCREEN_LABELS / sizeof SCREEN_LABELS[0]) ? SCREEN_LABELS[preset] : NULL;
}

bool dialog_rom_allows_screen(const dialog_rom_t *rom, int preset) {
    return preset >= 0 && preset < 32 && (rom->screens & (1u << preset));
}

bool dialog_rom_allows_memory(const dialog_rom_t *rom, int size) {
    return size >= 0 && size < DIALOG_MEMORY_COUNT && DIALOG_MEMORY_SIZES[size] <= rom->memory_max && DIALOG_MEMORY_SIZES[size] >= rom->memory_min;
}

int dialog_rom_memory_choice(const dialog_rom_t *rom, uint32_t megabytes) {
    int below = -1, above = -1;
    for (int i = 0; i < DIALOG_MEMORY_COUNT; i++) {
        if (!dialog_rom_allows_memory(rom, i)) continue;
        if (DIALOG_MEMORY_SIZES[i] <= megabytes) below = i;
        else if (above < 0) above = i;
    }
    return below >= 0 ? below : above;
}
