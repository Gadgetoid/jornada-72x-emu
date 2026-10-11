#include "core/state.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define STATE_MAGIC   "JORNADA720STATE"
#define STATE_VERSION 1u
#define TAG_SIZE      16

typedef struct {
    uint64_t host_time;
    uint64_t rom_hash;
    uint64_t pen_next_sample;
    uint32_t dram_size;
    uint8_t powered_off;
} state_header_t;

typedef struct {
    const char *tag;
    void *data;
    uint32_t size;
} record_t;

static uint64_t hash_bytes(const uint8_t *bytes, size_t length) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static bool write_record(gzFile file, const char *tag, const void *data, uint32_t size) {
    char name[TAG_SIZE] = { 0 };
    strncpy(name, tag, TAG_SIZE - 1);
    return gzwrite(file, name, TAG_SIZE) == TAG_SIZE && gzwrite(file, &size, sizeof size) == sizeof size
           && (!size || gzwrite(file, data, size) == (int)size);
}

bool state_save(const machine_t *machine, const char *path, uint64_t host_time) {
    size_t temporary_length = strlen(path) + 5;
    char *temporary = malloc(temporary_length);
    if (!temporary) return false;
    snprintf(temporary, temporary_length, "%s.tmp", path);
    gzFile file = gzopen(temporary, "wb1");
    if (!file) {
        free(temporary);
        return false;
    }
    state_header_t header = {
        .host_time = host_time,
        .rom_hash = hash_bytes(machine->rom, MACHINE_ROM_SIZE),
        .pen_next_sample = machine->pen_next_sample,
        .dram_size = machine->dram_size,
        .powered_off = machine->powered_off,
    };
    uint32_t version = STATE_VERSION;
    bool ok = gzwrite(file, STATE_MAGIC, TAG_SIZE) == TAG_SIZE && gzwrite(file, &version, sizeof version) == sizeof version;
    ok = ok && write_record(file, "header", &header, sizeof header);
    ok = ok && write_record(file, "cpu", &machine->cpu, ARM_STATE_SIZE);
    ok = ok && write_record(file, "sa1110", &machine->soc, sizeof machine->soc);
    ok = ok && write_record(file, "sa1111", &machine->sa1111, sizeof machine->sa1111);
    ok = ok && write_record(file, "sed1356", &machine->display, sizeof machine->display);
    ok = ok && write_record(file, "eeprom", &machine->eeprom, sizeof machine->eeprom);
    ok = ok && write_record(file, "mcu", &machine->mcu, sizeof machine->mcu);
    ok = ok && write_record(file, "dram", machine->dram, machine->dram_size);
    ok = ok && write_record(file, "end", NULL, 0);
    if (gzclose(file) != Z_OK) ok = false;
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) remove(temporary);
    free(temporary);
    return ok;
}

state_result_t state_load(machine_t *machine, const char *path, uint64_t host_time) {
    FILE *probe = fopen(path, "rb");
    if (!probe) return STATE_MISSING;
    fclose(probe);
    gzFile file = gzopen(path, "rb");
    if (!file) return STATE_UNREADABLE;
    char magic[TAG_SIZE];
    uint32_t version;
    if (gzread(file, magic, TAG_SIZE) != TAG_SIZE || memcmp(magic, STATE_MAGIC, TAG_SIZE) != 0
        || gzread(file, &version, sizeof version) != sizeof version) {
        gzclose(file);
        return STATE_UNREADABLE;
    }
    if (version != STATE_VERSION) {
        gzclose(file);
        return STATE_INCOMPATIBLE;
    }
    state_header_t header;
    arm_cpu_t *cpu = malloc(sizeof *cpu);
    sa1110_t *soc = malloc(sizeof *soc);
    sed1356_t *display = malloc(sizeof *display);
    uint8_t *dram = malloc(machine->dram_size);
    sa1111_t sa1111;
    eeprom_t eeprom;
    mcu_t mcu;
    record_t records[] = {
        { "header", &header, sizeof header },
        { "cpu", cpu, ARM_STATE_SIZE },
        { "sa1110", soc, sizeof *soc },
        { "sa1111", &sa1111, sizeof sa1111 },
        { "sed1356", display, sizeof *display },
        { "eeprom", &eeprom, sizeof eeprom },
        { "mcu", &mcu, sizeof mcu },
        { "dram", dram, machine->dram_size },
    };
    int record_count = (int)(sizeof records / sizeof records[0]);
    bool seen[sizeof records / sizeof records[0]] = { false };
    state_result_t result = STATE_OK;
    if (!cpu || !soc || !display || !dram) result = STATE_UNREADABLE;
    while (result == STATE_OK) {
        char tag[TAG_SIZE];
        uint32_t size;
        if (gzread(file, tag, TAG_SIZE) != TAG_SIZE || gzread(file, &size, sizeof size) != sizeof size) {
            result = STATE_UNREADABLE;
            break;
        }
        tag[TAG_SIZE - 1] = 0;
        if (!strcmp(tag, "end")) break;
        int index = -1;
        for (int i = 0; i < record_count; i++)
            if (!strcmp(tag, records[i].tag)) index = i;
        if (index < 0) {
            if (gzseek(file, size, SEEK_CUR) < 0) result = STATE_UNREADABLE;
            continue;
        }
        if (!strcmp(tag, "cpu") && size > ARM_STATE_SIZE) {
            uint8_t *legacy = malloc(size);
            if (!legacy || gzread(file, legacy, size) != (int)size) {
                free(legacy);
                result = STATE_UNREADABLE;
                break;
            }
            memset(cpu, 0, ARM_STATE_SIZE);
            memcpy(cpu, legacy, offsetof(arm_cpu_t, data_tlb));
            free(legacy);
            seen[index] = true;
            continue;
        }
        if (size > records[index].size || (size < records[index].size && !strcmp(tag, "dram"))) {
            result = STATE_INCOMPATIBLE;
            break;
        }
        if (size < records[index].size) memset(records[index].data, 0, records[index].size);
        if (gzread(file, records[index].data, size) != (int)size) {
            result = STATE_UNREADABLE;
            break;
        }
        seen[index] = true;
    }
    gzclose(file);
    for (int i = 0; i < record_count && result == STATE_OK; i++)
        if (!seen[i]) result = STATE_INCOMPATIBLE;
    if (result == STATE_OK && header.dram_size != machine->dram_size) result = STATE_INCOMPATIBLE;
    if (result == STATE_OK && header.rom_hash != hash_bytes(machine->rom, MACHINE_ROM_SIZE)) result = STATE_WRONG_ROM;
    if (result == STATE_OK) {
        memcpy(&machine->cpu, cpu, ARM_STATE_SIZE);
        memset(machine->cpu.read_cache, 0, sizeof machine->cpu.read_cache);
        memset(machine->cpu.write_cache, 0, sizeof machine->cpu.write_cache);
        memset(machine->cpu.fetch_cache, 0, sizeof machine->cpu.fetch_cache);
        machine->cpu.generation = 1;
        sa1110_board_t soc_board = machine->soc.board;
        machine->soc = *soc;
        machine->soc.board = soc_board;
        sa1111_board_t sa1111_board = machine->sa1111.board;
        sa1111_board.cpu_hz = MACHINE_CPU_HZ;
        machine->sa1111 = sa1111;
        machine->sa1111.board = sa1111_board;
        machine->display = *display;
        machine->eeprom = eeprom;
        uint16_t main_battery = machine->mcu.main_battery, backup_battery = machine->mcu.backup_battery;
        machine->mcu = mcu;
        machine->mcu.main_battery = main_battery;
        machine->mcu.backup_battery = backup_battery;
        memcpy(machine->dram, dram, machine->dram_size);
        machine->pen_next_sample = header.pen_next_sample;
        machine->powered_off = header.powered_off;
        machine->unknown_count = 0;
        machine_restore_serial_detect(machine);
        machine->mailbox_received_at = 0;
        machine->mailbox_fault_tries = 0;
        mailbox_clear_queue(&machine->mailbox.to_guest_from_emulator);
        mailbox_clear_queue(&machine->mailbox.to_emulator);
        if (host_time > header.host_time) machine->soc.rcnr += (uint32_t)(host_time - header.host_time);
    }
    free(cpu);
    free(soc);
    free(display);
    free(dram);
    return result;
}

uint64_t state_rom_hash(const machine_t *machine) {
    return hash_bytes(machine->rom, MACHINE_ROM_SIZE);
}

bool state_matches(const machine_t *machine, const char *path) {
    gzFile file = gzopen(path, "rb");
    if (!file) return false;
    char magic[TAG_SIZE], tag[TAG_SIZE];
    uint32_t version, size;
    state_header_t header;
    bool readable = gzread(file, magic, TAG_SIZE) == TAG_SIZE && !memcmp(magic, STATE_MAGIC, TAG_SIZE)
                    && gzread(file, &version, sizeof version) == sizeof version && version == STATE_VERSION
                    && gzread(file, tag, TAG_SIZE) == TAG_SIZE && !memcmp(tag, "header", sizeof "header")
                    && gzread(file, &size, sizeof size) == sizeof size && size == sizeof header
                    && gzread(file, &header, sizeof header) == sizeof header;
    gzclose(file);
    return readable && header.dram_size == machine->dram_size && header.rom_hash == state_rom_hash(machine);
}

const char *state_describe(state_result_t result) {
    switch (result) {
    case STATE_OK: return "state loaded";
    case STATE_MISSING: return "no saved state";
    case STATE_UNREADABLE: return "saved state unreadable";
    case STATE_INCOMPATIBLE: return "saved state is from a different build";
    case STATE_WRONG_ROM: return "saved state is for a different ROM";
    }
    return "";
}
