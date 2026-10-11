#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NATIVE_PAGE 1024u

typedef struct {
    void *context;
    uint8_t *(*map)(void *context, uint32_t va, bool write);
} native_memory_t;

typedef struct {
    uint32_t value;
    uint64_t cycles;
} native_result_t;

typedef bool (*native_fn)(const native_memory_t *memory, const uint32_t *arguments, native_result_t *result);

bool native_memmove(const native_memory_t *memory, const uint32_t *arguments, native_result_t *result);
uint64_t native_memmove_cycles(uint32_t length);
bool native_cache_sweep(const native_memory_t *memory, uint32_t *last_value);