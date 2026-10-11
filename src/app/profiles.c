#include "app/profiles.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util/file.h"

static void copy_value(char *destination, size_t size, const char *value) {
    size_t length = strcspn(value, "\r\n");
    if (length >= size) length = size - 1;
    memcpy(destination, value, length);
    destination[length] = 0;
}

static bool read_profile(const char *path, profile_t *profile) {
    FILE *file = fopen(path, "r");
    if (!file) return false;
    memset(profile, 0, sizeof *profile);
    profile->host_time = true;
    char line[1200];
    unsigned value;
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "name=", 5)) copy_value(profile->name, sizeof profile->name, line + 5);
        else if (!strncmp(line, "rom=", 4)) copy_value(profile->rom, sizeof profile->rom, line + 4);
        else if (!strncmp(line, "eeprom=", 7)) copy_value(profile->eeprom, sizeof profile->eeprom, line + 7);
        else if (!strncmp(line, "state=", 6)) copy_value(profile->state, sizeof profile->state, line + 6);
        else if (sscanf(line, "host_time=%u", &value) == 1) profile->host_time = value != 0;
    }
    fclose(file);
    return profile->name[0] && profile->rom[0] && profile->state[0];
}

static int compare_profiles(const void *a, const void *b) {
    return strcasecmp(((const profile_t *)a)->name, ((const profile_t *)b)->name);
}

void profiles_load(profiles_t *profiles, const char *folder) {
    profiles->count = 0;
    DIR *dir = opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && profiles->count < PROFILES_MAX) {
        size_t length = strlen(entry->d_name);
        if (length < 5 || strcmp(entry->d_name + length - 4, ".ini")) continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        profile_t *profile = &profiles->entries[profiles->count];
        if (!read_profile(path, profile)) continue;
        snprintf(profile->id, sizeof profile->id, "%.*s", (int)(length - 4), entry->d_name);
        profiles->count++;
    }
    closedir(dir);
    qsort(profiles->entries, (size_t)profiles->count, sizeof profiles->entries[0], compare_profiles);
}

static void profile_path(const profile_t *profile, const char *folder, char *path, size_t size) {
    snprintf(path, size, "%s/%s.ini", folder, profile->id);
}

bool profile_save(const profile_t *profile, const char *folder) {
    char path[1400], temporary[1500];
    profile_path(profile, folder, path, sizeof path);
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file) return false;
    fprintf(file, "name=%s\nrom=%s\neeprom=%s\nstate=%s\nhost_time=%u\n", profile->name, profile->rom, profile->eeprom, profile->state,
            profile->host_time ? 1u : 0u);
    bool written = fclose(file) == 0;
    if (written) written = rename(temporary, path) == 0;
    else remove(temporary);
    return written;
}

bool profile_delete(const profile_t *profile, const char *folder) {
    char path[1400];
    profile_path(profile, folder, path, sizeof path);
    remove(profile->state);
    return remove(path) == 0;
}

int profile_find(const profiles_t *profiles, const char *id_or_name) {
    for (int i = 0; i < profiles->count; i++) {
        if (!strcmp(profiles->entries[i].id, id_or_name) || !strcasecmp(profiles->entries[i].name, id_or_name)) return i;
    }
    return -1;
}

void profile_default_name(const profile_t *profile, char *name, size_t size) {
    char rom[128];
    snprintf(rom, sizeof rom, "%s", file_leaf_name(profile->rom));
    char *extension = strrchr(rom, '.');
    if (extension && extension != rom) *extension = 0;
    snprintf(name, size, "Jornada 720 (%s)", rom);
}

static bool name_taken(const profiles_t *profiles, const char *name) {
    for (int i = 0; i < profiles->count; i++) {
        if (!strcasecmp(profiles->entries[i].name, name)) return true;
    }
    return false;
}

static bool id_taken(const profiles_t *profiles, const char *id, const char *folder) {
    for (int i = 0; i < profiles->count; i++) {
        if (!strcmp(profiles->entries[i].id, id)) return true;
    }
    char path[1400];
    snprintf(path, sizeof path, "%s/%s.ini", folder, id);
    FILE *file = fopen(path, "r");
    if (file) fclose(file);
    return file != NULL;
}

void profile_make_unique(const profiles_t *profiles, profile_t *profile, const char *folder) {
    char base[96];
    snprintf(base, sizeof base, "%s", profile->name);
    for (int n = 2; name_taken(profiles, profile->name) && n < 100; n++) snprintf(profile->name, sizeof profile->name, "%.88s %d", base, n);
    char slug[48];
    size_t length = 0;
    for (const char *c = profile->name; *c && length < sizeof slug - 1; c++) {
        if (isalnum((unsigned char)*c)) slug[length++] = (char)tolower((unsigned char)*c);
        else if (length && slug[length - 1] != '-') slug[length++] = '-';
    }
    while (length && slug[length - 1] == '-') length--;
    slug[length] = 0;
    if (!length) snprintf(slug, sizeof slug, "machine");
    snprintf(profile->id, sizeof profile->id, "%s", slug);
    for (int n = 2; id_taken(profiles, profile->id, folder) && n < 1000; n++) snprintf(profile->id, sizeof profile->id, "%s-%d", slug, n);
    if (!profile->state[0]) snprintf(profile->state, sizeof profile->state, "%s/%s.state", folder, profile->id);
}
