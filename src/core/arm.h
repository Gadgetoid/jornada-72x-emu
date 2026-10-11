#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARM_MODE_USR 0x10u
#define ARM_MODE_FIQ 0x11u
#define ARM_MODE_IRQ 0x12u
#define ARM_MODE_SVC 0x13u
#define ARM_MODE_ABT 0x17u
#define ARM_MODE_UND 0x1Bu
#define ARM_MODE_SYS 0x1Fu

#define ARM_PSR_N (1u << 31)
#define ARM_PSR_Z (1u << 30)
#define ARM_PSR_C (1u << 29)
#define ARM_PSR_V (1u << 28)
#define ARM_PSR_I (1u << 7)
#define ARM_PSR_F (1u << 6)
#define ARM_PSR_MODE 0x1Fu

#define ARM_CONTROL_M (1u << 0)
#define ARM_CONTROL_A (1u << 1)
#define ARM_CONTROL_S (1u << 8)
#define ARM_CONTROL_R (1u << 9)
#define ARM_CONTROL_V (1u << 13)

enum {
    ARM_BANK_USR,
    ARM_BANK_FIQ,
    ARM_BANK_IRQ,
    ARM_BANK_SVC,
    ARM_BANK_ABT,
    ARM_BANK_UND,
    ARM_BANK_COUNT,
};

typedef struct {
    void    *context;
    bool (*read)(void *context, uint32_t pa, int size, uint32_t *value);
    bool (*write)(void *context, uint32_t pa, int size, uint32_t value);
    uint8_t *(*page)(void *context, uint32_t pa, bool write);
} arm_bus_t;

#define ARM_PAGE_CACHE 1024

#define ARM_TLB_ENTRIES 32

typedef struct {
    uint32_t tag;
    uint32_t generation;
    uint32_t pa;
    uint32_t serial;
    int32_t slot;
    uint8_t *host;
} arm_page_cache_t;

typedef struct {
    uint32_t base;
    uint32_t mask;
    uint32_t pa;
    uint32_t serial;
    uint8_t ap[4];
    uint8_t subpage_shift;
    uint8_t domain;
    uint8_t section;
} arm_tlb_entry_t;

typedef struct {
    arm_tlb_entry_t entries[ARM_TLB_ENTRIES];
    uint32_t next;
} arm_tlb_t;

typedef struct arm_cpu arm_cpu_t;

struct arm_cpu {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t spsr[ARM_BANK_COUNT];
    uint32_t banked_r13[ARM_BANK_COUNT];
    uint32_t banked_r14[ARM_BANK_COUNT];
    uint32_t user_r8_12[5];
    uint32_t fiq_r8_12[5];

    uint32_t control;
    uint32_t ttb;
    uint32_t dacr;
    uint32_t fsr;
    uint32_t far;
    uint32_t pid;
    uint32_t prefetched_control;
    int prefetched;

    bool irq_line;
    bool fiq_line;
    bool waiting;
    bool yield;

    uint64_t cycles;
    uint64_t instructions;
    uint32_t generation;
    arm_tlb_t data_tlb;
    arm_tlb_t instruction_tlb;
    uint32_t tlb_serial;
    uint32_t last_pc;
    uint32_t next_pc;
    uint32_t undefined_count;

    arm_bus_t bus;
    const uint8_t *optimiser_filter;
    void *optimiser_context;
    bool (*on_optimiser)(void *context, arm_cpu_t *cpu, uint32_t pc);
    bool (*on_swi)(void *context, uint32_t comment);
    arm_page_cache_t read_cache[2][ARM_PAGE_CACHE];
    arm_page_cache_t write_cache[2][ARM_PAGE_CACHE];
    arm_page_cache_t fetch_cache[2][ARM_PAGE_CACHE];

    void *hook_context;
    void (*on_undefined)(void *context, uint32_t pc, uint32_t op);
    bool (*on_cp15)(void *context, uint32_t op, uint32_t value);
    bool (*before)(void *context, uint32_t pc);
    void (*on_exception)(void *context, uint32_t vector, uint32_t pc);
};

#define ARM_STATE_SIZE offsetof(arm_cpu_t, bus)

void arm_reset(arm_cpu_t *cpu);
void arm_run(arm_cpu_t *cpu, uint64_t until_cycle);
void arm_set_cpsr(arm_cpu_t *cpu, uint32_t value);
void arm_flush_translations(arm_cpu_t *cpu);
bool arm_translate(arm_cpu_t *cpu, uint32_t va, bool write, bool user, uint32_t *pa, uint32_t *fault);
uint8_t *arm_map_virtual(arm_cpu_t *cpu, uint32_t va, bool write, bool user);
void arm_raise_data_abort(arm_cpu_t *cpu, uint32_t va, bool write);
bool arm_read_instruction_virtual(arm_cpu_t *cpu, uint32_t va, uint32_t *value);
bool arm_read_virtual(arm_cpu_t *cpu, uint32_t va, int size, uint32_t *value);
bool arm_write_virtual(arm_cpu_t *cpu, uint32_t va, int size, uint32_t value);
uint32_t arm_register(const arm_cpu_t *cpu, int index);
