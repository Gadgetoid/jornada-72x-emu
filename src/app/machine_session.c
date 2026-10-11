#include "app/machine_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/paths.h"
#include "util/file.h"

#define LEGACY_STATE "state.bin"

void machine_session_state_path(char *path, size_t size, machine_t *machine, const char *rom_path) {
    char base[1024];
    app_data_folder(base, sizeof base);
    char rom_name[256];
    snprintf(rom_name, sizeof rom_name, "%s", file_leaf_name(rom_path));
    char *extension = strrchr(rom_name, '.');
    if (extension && extension != rom_name) *extension = 0;
    snprintf(path, size, "%s/state-%s-%08x.bin", base, rom_name, (uint32_t)machine_rom_hash(machine));
}

static bool legacy_state_path(const rom_set_t *roms, char *state, size_t size) {
    char base[1024];
    app_data_folder(base, sizeof base);
    snprintf(state, size, "%s/" LEGACY_STATE, base);
    FILE *existing = fopen(state, "rb");
    if (!existing) return false;
    fclose(existing);
    char error[256];
    machine_t *machine = machine_create(roms->rom, roms->eeprom, error, sizeof error);
    bool matches = machine && machine_state_matches(machine, state);
    machine_destroy(machine);
    return matches;
}

void machine_session_migrate_profiles(profiles_t *profiles, const rom_set_t *roms, const settings_t *settings, const char *folder) {
    if (!roms->rom[0]) return;
    profile_t profile = { .host_time = settings->host_time != 0 };
    snprintf(profile.name, sizeof profile.name, "Jornada 720");
    snprintf(profile.rom, sizeof profile.rom, "%s", roms->rom);
    snprintf(profile.eeprom, sizeof profile.eeprom, "%s", roms->eeprom);
    if (!legacy_state_path(roms, profile.state, sizeof profile.state)) profile.state[0] = 0;
    profile_make_unique(profiles, &profile, folder);
    profile_save(&profile, folder);
    profiles_load(profiles, folder);
}

bool machine_session_start(machine_session_t *session, const profile_t *profile, uint32_t speed, const char *state_file, bool fresh,
                           snapshot_store_t *snapshots, char *notice, size_t notice_size) {
    memset(session, 0, sizeof *session);
    notice[0] = 0;
    char error[256];
    machine_t *machine = machine_create(profile->rom, profile->eeprom, error, sizeof error);
    if (!machine) {
        snprintf(notice, notice_size, "%s", error);
        return false;
    }
    machine_set_speed(machine, speed);
    machine_set_host_clock(machine, profile->host_time);
    if (state_file) snprintf(session->state_path, sizeof session->state_path, "%s", state_file);
    else if (profile->state[0]) snprintf(session->state_path, sizeof session->state_path, "%s", profile->state);
    else machine_session_state_path(session->state_path, sizeof session->state_path, machine, profile->rom);
    session->machine = machine;
    if (fresh) {
        snapshot_store_backup_file(snapshots, session->state_path);
        return true;
    }
    if (machine_load(machine, session->state_path, (int64_t)time(NULL))) return true;
    FILE *existing = fopen(session->state_path, "rb");
    if (existing) {
        fclose(existing);
        char backup[1200];
        snprintf(backup, sizeof backup, "%s.old", session->state_path);
        rename(session->state_path, backup);
        snprintf(notice, notice_size, "saved state unreadable, moved to %s", file_leaf_name(backup));
    }
    return true;
}

void machine_session_destroy(machine_session_t *session) {
    machine_destroy(session->machine);
    memset(session, 0, sizeof *session);
}
