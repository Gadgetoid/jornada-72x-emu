#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/machine.h"

#define CE_PROCESS_MAX 32
#define CE_NAME_MAX    64
#define CE_CURRENT     (-1)

#define CE_MODULE_MAX  64
#define CE_SLOT_SIZE   0x02000000u

typedef struct {
    machine_t *machine;
    uint32_t kdata;
    uint32_t process_array;
    uint32_t process_stride;
    uint32_t page_size;
    uint32_t pfn_mask;
} ce_t;

typedef struct {
    char name[CE_NAME_MAX];
    uint32_t base;
    uint32_t in_use;
    uint32_t entry;
} ce_module_t;

void ce_init(ce_t *ce, machine_t *machine);
bool ce_ready(ce_t *ce);
int  ce_current_process(ce_t *ce);
bool ce_process_name(ce_t *ce, int process, char *name, size_t size);
int  ce_find_process(ce_t *ce, const char *name);
bool ce_translate(ce_t *ce, uint32_t va, int process, bool write, uint32_t *pa);
bool ce_read(ce_t *ce, uint32_t va, int process, uint8_t *data, uint32_t length);
bool ce_write(ce_t *ce, uint32_t va, int process, const uint8_t *data, uint32_t length);
bool ce_read_word(ce_t *ce, uint32_t va, int process, uint32_t *value);
int  ce_modules(ce_t *ce, ce_module_t *modules, int max);
bool ce_first_module(ce_t *ce, uint32_t *module);
bool ce_module_entry(ce_t *ce, uint32_t module, uint32_t *entry);
bool ce_find_fault_report(ce_t *ce, uint32_t *va);
