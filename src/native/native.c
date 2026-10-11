#include "native/native.h"

#include <string.h>

#define NATIVE_MOVE_MAX 0x04000000u
#define CACHE_MAIN_BASE 0x85000000u
#define CACHE_MAIN_SIZE 0x2000u
#define CACHE_MINI_BASE 0x85100000u
#define CACHE_MINI_SIZE 0x400u
#define CACHE_SWEEP_CYCLES 870u

uint64_t native_memmove_cycles(uint32_t length) {
    if (length < 8) return 12 + (uint64_t)length * 3;
    if (length < 32) return 8 + ((uint64_t)length * 11 + 2) / 5;
    return 10 + ((uint64_t)length * 4 + 2) / 5;
}

bool native_cache_sweep(const native_memory_t *memory, uint32_t *last_value) {
    for (uint32_t offset = 0; offset < CACHE_MAIN_SIZE; offset += NATIVE_PAGE)
        if (!memory->map(memory->context, CACHE_MAIN_BASE + offset, false)) return false;
    if (!memory->map(memory->context, CACHE_MINI_BASE, false)) return false;
    uint8_t *last = memory->map(memory->context, CACHE_MINI_BASE + CACHE_MINI_SIZE - 32, false);
    if (!last) return false;
    memcpy(last_value, last, sizeof *last_value);
    return true;
}

static bool range_mapped(const native_memory_t *memory, uint32_t va, uint32_t length, bool write) {
    if (!length) return true;
    if (length > UINT32_MAX - va) return false;
    uint32_t end = va + length;
    while (va < end) {
        if (!memory->map(memory->context, va, write)) return false;
        uint32_t chunk = NATIVE_PAGE - (va & (NATIVE_PAGE - 1));
        if (chunk > end - va) chunk = end - va;
        va += chunk;
    }
    return true;
}

bool native_memmove(const native_memory_t *memory, const uint32_t *arguments, native_result_t *result) {
    uint32_t destination = arguments[0];
    uint32_t source = arguments[1];
    uint32_t length = arguments[2];
    if (length > NATIVE_MOVE_MAX) return false;
    if (!length) {
        result->value = destination;
        result->cycles = native_memmove_cycles(length);
        return true;
    }

    uint32_t source_offset = source & (NATIVE_PAGE - 1);
    uint32_t destination_offset = destination & (NATIVE_PAGE - 1);
    if (length <= NATIVE_PAGE - source_offset && length <= NATIVE_PAGE - destination_offset) {
        uint8_t *source_host = memory->map(memory->context, source, false);
        uint8_t *destination_host = memory->map(memory->context, destination, true);
        if (!source_host || !destination_host) return false;
        memmove(destination_host, source_host, length);
        result->value = destination;
        result->cycles = native_memmove_cycles(length);
        return true;
    }

    if (!range_mapped(memory, source, length, false) || !range_mapped(memory, destination, length, true)) return false;

    uint8_t buffer[NATIVE_PAGE];
    bool backwards = destination > source && destination - source < length;
    for (uint32_t completed = 0; completed < length;) {
        uint32_t offset;
        uint32_t chunk;
        if (backwards) {
            uint32_t remaining = length - completed;
            uint32_t source_room = ((source + remaining - 1) & (NATIVE_PAGE - 1)) + 1;
            uint32_t destination_room = ((destination + remaining - 1) & (NATIVE_PAGE - 1)) + 1;
            chunk = source_room < destination_room ? source_room : destination_room;
            if (chunk > remaining) chunk = remaining;
            offset = remaining - chunk;
        } else {
            offset = completed;
            uint32_t remaining = length - completed;
            uint32_t source_room = NATIVE_PAGE - ((source + offset) & (NATIVE_PAGE - 1));
            uint32_t destination_room = NATIVE_PAGE - ((destination + offset) & (NATIVE_PAGE - 1));
            chunk = source_room < destination_room ? source_room : destination_room;
            if (chunk > remaining) chunk = remaining;
        }
        uint8_t *source_host = memory->map(memory->context, source + offset, false);
        uint8_t *destination_host = memory->map(memory->context, destination + offset, true);
        if (!source_host || !destination_host) return false;
        memcpy(buffer, source_host, chunk);
        memcpy(destination_host, buffer, chunk);
        completed += chunk;
    }
    result->value = destination;
    result->cycles = native_memmove_cycles(length);
    return true;
}