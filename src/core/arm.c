#include "core/arm.h"

#include <string.h>

#define VECTOR_RESET      0x00u
#define VECTOR_UNDEFINED  0x04u
#define VECTOR_SWI        0x08u
#define VECTOR_PREFETCH   0x0Cu
#define VECTOR_DATA_ABORT 0x10u
#define VECTOR_IRQ        0x18u
#define VECTOR_FIQ        0x1Cu
#define HIGH_VECTORS      0xFFFF0000u

#define FCSE_LIMIT 0x02000000u
#define BLOCK_MASK 0x3FFu

#define FAULT_ALIGNMENT          0x1u
#define FAULT_TRANSLATION_SECTION 0x5u
#define FAULT_TRANSLATION_PAGE   0x7u
#define FAULT_DOMAIN_SECTION     0x9u
#define FAULT_DOMAIN_PAGE        0xBu
#define FAULT_PERMISSION_SECTION 0xDu
#define FAULT_PERMISSION_PAGE    0xFu
#define FAULT_EXTERNAL           0x8u

#define SA1110_ID 0x6901B119u

enum {
    ACCESS_READ,
    ACCESS_WRITE,
    ACCESS_FETCH,
};

static uint16_t condition_table[16];

static void tlb_flush(arm_tlb_t *tlb);

static void build_condition_table(void) {
    for (int flags = 0; flags < 16; flags++) {
        bool n = flags & 8, z = flags & 4, c = flags & 2, v = flags & 1;
        bool pass[16] = {
            z, !z, c, !c, n, !n, v, !v,
            c && !z, !c || z, n == v, n != v, !z && n == v, z || n != v, true, false,
        };
        for (int cond = 0; cond < 16; cond++)
            if (pass[cond]) condition_table[cond] |= 1u << flags;
    }
}

static int bank_of(uint32_t mode) {
    switch (mode) {
    case ARM_MODE_FIQ: return ARM_BANK_FIQ;
    case ARM_MODE_IRQ: return ARM_BANK_IRQ;
    case ARM_MODE_SVC: return ARM_BANK_SVC;
    case ARM_MODE_ABT: return ARM_BANK_ABT;
    case ARM_MODE_UND: return ARM_BANK_UND;
    default: return ARM_BANK_USR;
    }
}

static bool privileged(const arm_cpu_t *cpu) {
    return (cpu->cpsr & ARM_PSR_MODE) != ARM_MODE_USR;
}

static bool has_spsr(const arm_cpu_t *cpu) {
    return bank_of(cpu->cpsr & ARM_PSR_MODE) != ARM_BANK_USR;
}

void arm_set_cpsr(arm_cpu_t *cpu, uint32_t value) {
    int old_bank = bank_of(cpu->cpsr & ARM_PSR_MODE);
    int new_bank = bank_of(value & ARM_PSR_MODE);
    if (old_bank != new_bank) {
        if (old_bank == ARM_BANK_FIQ) memcpy(cpu->fiq_r8_12, &cpu->r[8], sizeof cpu->fiq_r8_12);
        else memcpy(cpu->user_r8_12, &cpu->r[8], sizeof cpu->user_r8_12);
        cpu->banked_r13[old_bank] = cpu->r[13];
        cpu->banked_r14[old_bank] = cpu->r[14];
        if (new_bank == ARM_BANK_FIQ) memcpy(&cpu->r[8], cpu->fiq_r8_12, sizeof cpu->fiq_r8_12);
        else memcpy(&cpu->r[8], cpu->user_r8_12, sizeof cpu->user_r8_12);
        cpu->r[13] = cpu->banked_r13[new_bank];
        cpu->r[14] = cpu->banked_r14[new_bank];
    }
    cpu->cpsr = value;
}

void arm_flush_translations(arm_cpu_t *cpu) {
    if (++cpu->generation == 0) {
        memset(cpu->read_cache, 0, sizeof cpu->read_cache);
        memset(cpu->write_cache, 0, sizeof cpu->write_cache);
        memset(cpu->fetch_cache, 0, sizeof cpu->fetch_cache);
        cpu->generation = 1;
    }
}

void arm_reset(arm_cpu_t *cpu) {
    if (!condition_table[14]) build_condition_table();
    memset(cpu->r, 0, sizeof cpu->r);
    memset(cpu->spsr, 0, sizeof cpu->spsr);
    memset(cpu->banked_r13, 0, sizeof cpu->banked_r13);
    memset(cpu->banked_r14, 0, sizeof cpu->banked_r14);
    memset(cpu->user_r8_12, 0, sizeof cpu->user_r8_12);
    memset(cpu->fiq_r8_12, 0, sizeof cpu->fiq_r8_12);
    cpu->cpsr = ARM_MODE_SVC | ARM_PSR_I | ARM_PSR_F;
    cpu->control = 0;
    cpu->ttb = cpu->dacr = cpu->fsr = cpu->far = cpu->pid = 0;
    cpu->waiting = false;
    cpu->r[15] = VECTOR_RESET;
    tlb_flush(&cpu->data_tlb);
    tlb_flush(&cpu->instruction_tlb);
    arm_flush_translations(cpu);
}

static uint32_t modified_address(const arm_cpu_t *cpu, uint32_t va) {
    return va < FCSE_LIMIT ? va | cpu->pid : va;
}

static bool physical_word(arm_cpu_t *cpu, uint32_t pa, uint32_t *value) {
    uint8_t *host = cpu->bus.page(cpu->bus.context, pa & ~BLOCK_MASK, false);
    if (host) {
        memcpy(value, host + (pa & BLOCK_MASK), 4);
        return true;
    }
    return cpu->bus.read(cpu->bus.context, pa, 4, value);
}

static bool permitted(const arm_cpu_t *cpu, uint32_t ap, bool write, bool user) {
    switch (ap) {
    case 0: {
        bool system = cpu->control & ARM_CONTROL_S, rom = cpu->control & ARM_CONTROL_R;
        if (system && !rom) return !user && !write;
        if (rom && !system) return !write;
        return false;
    }
    case 1: return !user;
    case 2: return !user || !write;
    default: return true;
    }
}

static bool descriptor(arm_cpu_t *cpu, uint32_t mva, arm_tlb_entry_t *entry, uint32_t *fault) {
    uint32_t first;
    if (!physical_word(cpu, (cpu->ttb & 0xFFFFC000u) | ((mva >> 18) & 0x3FFCu), &first)) {
        *fault = 0xCu;
        return false;
    }
    uint32_t domain = (first >> 5) & 15;
    entry->domain = (uint8_t)domain;
    entry->section = 0;
    entry->subpage_shift = 0;
    switch (first & 3) {
    case 0:
        *fault = FAULT_TRANSLATION_SECTION;
        return false;
    case 2:
        entry->section = 1;
        entry->mask = 0x000FFFFFu;
        entry->pa = first & 0xFFF00000u;
        memset(entry->ap, (first >> 10) & 3, sizeof entry->ap);
        break;
    default: {
        uint32_t second_address = (first & 3) == 1
            ? (first & 0xFFFFFC00u) | ((mva >> 10) & 0x3FCu)
            : (first & 0xFFFFF000u) | ((mva >> 8) & 0xFFCu);
        uint32_t second;
        if (!physical_word(cpu, second_address, &second)) {
            *fault = 0xEu | (domain << 4);
            return false;
        }
        switch (second & 3) {
        case 0:
            *fault = FAULT_TRANSLATION_PAGE | (domain << 4);
            return false;
        case 1:
            entry->mask = 0x0000FFFFu;
            entry->pa = second & 0xFFFF0000u;
            entry->subpage_shift = 14;
            break;
        case 2:
            entry->mask = 0x00000FFFu;
            entry->pa = second & 0xFFFFF000u;
            entry->subpage_shift = 10;
            break;
        default:
            entry->mask = 0x000003FFu;
            entry->pa = second & 0xFFFFFC00u;
            break;
        }
        for (int subpage = 0; subpage < 4; subpage++)
            entry->ap[subpage] = (uint8_t)((second >> (4 + subpage * 2)) & 3);
        if ((second & 3) == 3) memset(entry->ap, entry->ap[0], sizeof entry->ap);
    }
    }
    entry->base = mva & ~entry->mask;
    return true;
}

static bool access_allowed(const arm_cpu_t *cpu, const arm_tlb_entry_t *entry, uint32_t mva, bool write, bool user,
                           uint32_t *fault) {
    uint32_t domain = entry->domain;
    uint32_t ap = entry->ap[entry->subpage_shift ? (mva >> entry->subpage_shift) & 3 : 0];
    switch ((cpu->dacr >> (domain * 2)) & 3) {
    case 3:
        return true;
    case 1:
        if (permitted(cpu, ap, write, user)) return true;
        *fault = (entry->section ? FAULT_PERMISSION_SECTION : FAULT_PERMISSION_PAGE) | (domain << 4);
        return false;
    default:
        *fault = (entry->section ? FAULT_DOMAIN_SECTION : FAULT_DOMAIN_PAGE) | (domain << 4);
        return false;
    }
}

static bool walk(arm_cpu_t *cpu, uint32_t mva, bool write, bool user, uint32_t *pa, uint32_t *fault) {
    if (!(cpu->control & ARM_CONTROL_M)) {
        *pa = mva;
        return true;
    }
    arm_tlb_entry_t entry;
    if (!descriptor(cpu, mva, &entry, fault) || !access_allowed(cpu, &entry, mva, write, user, fault)) return false;
    *pa = entry.pa | (mva & entry.mask);
    return true;
}

static int tlb_find(const arm_tlb_t *tlb, uint32_t mva) {
    for (int i = 0; i < ARM_TLB_ENTRIES; i++) {
        const arm_tlb_entry_t *entry = &tlb->entries[i];
        if (entry->serial && (mva & ~entry->mask) == entry->base) return i;
    }
    return -1;
}

static void tlb_flush(arm_tlb_t *tlb) {
    for (int i = 0; i < ARM_TLB_ENTRIES; i++) tlb->entries[i].serial = 0;
}

static int tlb_translate(arm_cpu_t *cpu, arm_tlb_t *tlb, uint32_t mva, uint32_t *fault) {
    int slot = tlb_find(tlb, mva);
    if (slot >= 0) return slot;
    arm_tlb_entry_t entry;
    if (!descriptor(cpu, mva, &entry, fault)) return -1;
    slot = (int)tlb->next;
    tlb->next = (tlb->next + 1) % ARM_TLB_ENTRIES;
    if (++cpu->tlb_serial == 0) cpu->tlb_serial = 1;
    entry.serial = cpu->tlb_serial;
    tlb->entries[slot] = entry;
    return slot;
}

bool arm_translate(arm_cpu_t *cpu, uint32_t va, bool write, bool user, uint32_t *pa, uint32_t *fault) {
    uint32_t ignored;
    uint32_t mva = modified_address(cpu, va);
    if (!fault) fault = &ignored;
    if (cpu->control & ARM_CONTROL_M) {
        int slot = tlb_find(&cpu->data_tlb, mva);
        if (slot >= 0) {
            arm_tlb_entry_t *entry = &cpu->data_tlb.entries[slot];
            if (!access_allowed(cpu, entry, mva, write, user, fault)) return false;
            *pa = entry->pa | (mva & entry->mask);
            return true;
        }
    }
    return walk(cpu, mva, write, user, pa, fault);
}

static arm_page_cache_t *lookup(arm_cpu_t *cpu, int access, uint32_t mva, bool user, uint32_t *fault) {
    arm_page_cache_t *table = access == ACCESS_WRITE ? cpu->write_cache[user]
        : access == ACCESS_FETCH ? cpu->fetch_cache[user] : cpu->read_cache[user];
    arm_page_cache_t *slot = &table[(mva >> 10) & (ARM_PAGE_CACHE - 1)];
    uint32_t tag = mva & ~BLOCK_MASK;
    arm_tlb_t *tlb = access == ACCESS_FETCH ? &cpu->instruction_tlb : &cpu->data_tlb;
    if (slot->generation == cpu->generation && slot->tag == tag
        && (slot->slot < 0 || tlb->entries[slot->slot].serial == slot->serial))
        return slot;
    uint32_t pa;
    int tlb_slot = -1;
    uint32_t serial = 0;
    if (cpu->control & ARM_CONTROL_M) {
        tlb_slot = tlb_translate(cpu, tlb, tag, fault);
        if (tlb_slot < 0) return NULL;
        arm_tlb_entry_t *entry = &tlb->entries[tlb_slot];
        if (!access_allowed(cpu, entry, tag, access == ACCESS_WRITE, user, fault)) return NULL;
        pa = entry->pa | (tag & entry->mask);
        serial = entry->serial;
    } else {
        pa = tag;
    }
    slot->tag = tag;
    slot->generation = cpu->generation;
    slot->slot = tlb_slot;
    slot->serial = serial;
    slot->pa = pa;
    slot->host = cpu->bus.page(cpu->bus.context, pa, access == ACCESS_WRITE);
    return slot;
}

uint8_t *arm_map_virtual(arm_cpu_t *cpu, uint32_t va, bool write, bool user) {
    uint32_t mva = modified_address(cpu, va), fault = 0;
    arm_page_cache_t *slot = lookup(cpu, write ? ACCESS_WRITE : ACCESS_READ, mva, user, &fault);
    if (!slot || !slot->host) return NULL;
    return slot->host + (mva & BLOCK_MASK);
}

static void raise_exception(arm_cpu_t *cpu, uint32_t mode, uint32_t vector, uint32_t link) {
    if (cpu->on_exception) cpu->on_exception(cpu->hook_context, vector, link);
    uint32_t saved = cpu->cpsr;
    uint32_t mask = ARM_PSR_I | (mode == ARM_MODE_FIQ ? ARM_PSR_F : 0);
    arm_set_cpsr(cpu, (saved & ~ARM_PSR_MODE) | mode | mask);
    cpu->spsr[bank_of(mode)] = saved;
    cpu->r[14] = link;
    cpu->next_pc = ((cpu->control & ARM_CONTROL_V) ? HIGH_VECTORS : 0) + vector;
}

static void data_abort(arm_cpu_t *cpu, uint32_t va, uint32_t fault) {
    cpu->fsr = fault;
    cpu->far = va;
    raise_exception(cpu, ARM_MODE_ABT, VECTOR_DATA_ABORT, cpu->last_pc + 8);
}

void arm_raise_data_abort(arm_cpu_t *cpu, uint32_t va, bool write) {
    uint32_t mva = modified_address(cpu, va), fault = FAULT_EXTERNAL;
    lookup(cpu, write ? ACCESS_WRITE : ACCESS_READ, mva, !privileged(cpu), &fault);
    data_abort(cpu, mva, fault);
}

static void undefined(arm_cpu_t *cpu, uint32_t op) {
    cpu->undefined_count++;
    if (cpu->on_undefined) cpu->on_undefined(cpu->hook_context, cpu->last_pc, op);
    raise_exception(cpu, ARM_MODE_UND, VECTOR_UNDEFINED, cpu->last_pc + 4);
}

static bool load(arm_cpu_t *cpu, uint32_t va, int size, uint32_t *value, bool user) {
    if ((cpu->control & ARM_CONTROL_A) && (va & (size - 1))) {
        data_abort(cpu, va, FAULT_ALIGNMENT);
        return false;
    }
    uint32_t mva = modified_address(cpu, va), fault = 0;
    arm_page_cache_t *slot = lookup(cpu, ACCESS_READ, mva, user, &fault);
    if (!slot) {
        data_abort(cpu, mva, fault);
        return false;
    }
    uint32_t offset = mva & BLOCK_MASK;
    if (slot->host) {
        uint8_t *at = slot->host + offset;
        if (size == 4) memcpy(value, at, 4);
        else if (size == 2) {
            uint16_t half;
            memcpy(&half, at, 2);
            *value = half;
        } else *value = *at;
        return true;
    }
    if (cpu->bus.read(cpu->bus.context, slot->pa | offset, size, value)) return true;
    data_abort(cpu, mva, FAULT_EXTERNAL);
    return false;
}

static bool store(arm_cpu_t *cpu, uint32_t va, int size, uint32_t value, bool user) {
    if ((cpu->control & ARM_CONTROL_A) && (va & (size - 1))) {
        data_abort(cpu, va, FAULT_ALIGNMENT);
        return false;
    }
    uint32_t mva = modified_address(cpu, va), fault = 0;
    arm_page_cache_t *slot = lookup(cpu, ACCESS_WRITE, mva, user, &fault);
    if (!slot) {
        data_abort(cpu, mva, fault);
        return false;
    }
    uint32_t offset = mva & BLOCK_MASK;
    if (slot->host) {
        uint8_t *at = slot->host + offset;
        if (size == 4) memcpy(at, &value, 4);
        else if (size == 2) {
            uint16_t half = (uint16_t)value;
            memcpy(at, &half, 2);
        } else *at = (uint8_t)value;
        return true;
    }
    if (cpu->bus.write(cpu->bus.context, slot->pa | offset, size, value)) return true;
    data_abort(cpu, mva, FAULT_EXTERNAL);
    return false;
}

static uint32_t rotate_right(uint32_t value, uint32_t amount) {
    amount &= 31;
    return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

static uint32_t transfer_address(const arm_cpu_t *cpu, uint32_t va, uint32_t mask) {
    return (cpu->control & ARM_CONTROL_A) ? va : va & ~mask;
}

static bool load_word(arm_cpu_t *cpu, uint32_t va, uint32_t *value, bool user) {
    if (!load(cpu, transfer_address(cpu, va, 3), 4, value, user)) return false;
    *value = rotate_right(*value, (va & 3) * 8);
    return true;
}

static void write_register(arm_cpu_t *cpu, int index, uint32_t value) {
    if (index == 15) cpu->next_pc = value & ~3u;
    else cpu->r[index] = value;
}

static uint32_t user_register(const arm_cpu_t *cpu, int index) {
    uint32_t mode = cpu->cpsr & ARM_PSR_MODE;
    if (index < 8 || index == 15 || mode == ARM_MODE_USR || mode == ARM_MODE_SYS) return cpu->r[index];
    if (index < 13) return mode == ARM_MODE_FIQ ? cpu->user_r8_12[index - 8] : cpu->r[index];
    return index == 13 ? cpu->banked_r13[ARM_BANK_USR] : cpu->banked_r14[ARM_BANK_USR];
}

static void set_user_register(arm_cpu_t *cpu, int index, uint32_t value) {
    uint32_t mode = cpu->cpsr & ARM_PSR_MODE;
    if (index == 15) write_register(cpu, 15, value);
    else if (index < 8 || mode == ARM_MODE_USR || mode == ARM_MODE_SYS) cpu->r[index] = value;
    else if (index < 13) {
        if (mode == ARM_MODE_FIQ) cpu->user_r8_12[index - 8] = value;
        else cpu->r[index] = value;
    } else if (index == 13) cpu->banked_r13[ARM_BANK_USR] = value;
    else cpu->banked_r14[ARM_BANK_USR] = value;
}

uint32_t arm_register(const arm_cpu_t *cpu, int index) {
    return cpu->r[index & 15];
}

static void set_nz(arm_cpu_t *cpu, uint32_t result) {
    cpu->cpsr = (cpu->cpsr & ~(ARM_PSR_N | ARM_PSR_Z)) | (result & ARM_PSR_N) | (result ? 0 : ARM_PSR_Z);
}

static uint32_t add_with_carry(arm_cpu_t *cpu, uint32_t a, uint32_t b, uint32_t carry_in, bool set_flags) {
    uint64_t wide = (uint64_t)a + b + carry_in;
    uint32_t result = (uint32_t)wide;
    if (set_flags) {
        uint32_t flags = (result & ARM_PSR_N) | (result ? 0 : ARM_PSR_Z);
        if (wide >> 32) flags |= ARM_PSR_C;
        if ((~(a ^ b) & (a ^ result)) >> 31) flags |= ARM_PSR_V;
        cpu->cpsr = (cpu->cpsr & 0x0FFFFFFFu) | flags;
    }
    return result;
}

static uint32_t shift_immediate(uint32_t value, uint32_t type, uint32_t amount, uint32_t carry_in, uint32_t *carry_out) {
    switch (type) {
    case 0:
        if (!amount) {
            *carry_out = carry_in;
            return value;
        }
        *carry_out = (value >> (32 - amount)) & 1;
        return value << amount;
    case 1:
        if (!amount) {
            *carry_out = value >> 31;
            return 0;
        }
        *carry_out = (value >> (amount - 1)) & 1;
        return value >> amount;
    case 2:
        if (!amount) {
            *carry_out = value >> 31;
            return (uint32_t)((int32_t)value >> 31);
        }
        *carry_out = (value >> (amount - 1)) & 1;
        return (uint32_t)((int32_t)value >> amount);
    default:
        if (!amount) {
            *carry_out = value & 1;
            return (carry_in << 31) | (value >> 1);
        }
        *carry_out = (value >> (amount - 1)) & 1;
        return rotate_right(value, amount);
    }
}

static uint32_t shift_register(uint32_t value, uint32_t type, uint32_t amount, uint32_t carry_in, uint32_t *carry_out) {
    if (!amount) {
        *carry_out = carry_in;
        return value;
    }
    switch (type) {
    case 0:
        if (amount < 32) {
            *carry_out = (value >> (32 - amount)) & 1;
            return value << amount;
        }
        *carry_out = amount == 32 ? value & 1 : 0;
        return 0;
    case 1:
        if (amount < 32) {
            *carry_out = (value >> (amount - 1)) & 1;
            return value >> amount;
        }
        *carry_out = amount == 32 ? value >> 31 : 0;
        return 0;
    case 2:
        if (amount < 32) {
            *carry_out = (value >> (amount - 1)) & 1;
            return (uint32_t)((int32_t)value >> amount);
        }
        *carry_out = value >> 31;
        return (uint32_t)((int32_t)value >> 31);
    default:
        amount &= 31;
        if (!amount) {
            *carry_out = value >> 31;
            return value;
        }
        *carry_out = (value >> (amount - 1)) & 1;
        return rotate_right(value, amount);
    }
}

static void data_processing(arm_cpu_t *cpu, uint32_t op) {
    uint32_t opcode = (op >> 21) & 15;
    bool set_flags = (op >> 20) & 1;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    uint32_t carry = (cpu->cpsr >> 29) & 1;
    uint32_t shifter_carry = carry;
    uint32_t operand;
    uint32_t first = cpu->r[rn];
    if (op & (1u << 25)) {
        uint32_t rotate = ((op >> 8) & 15) * 2;
        operand = rotate_right(op & 0xFF, rotate);
        if (rotate) shifter_carry = operand >> 31;
    } else {
        int rm = op & 15;
        uint32_t value = cpu->r[rm];
        uint32_t type = (op >> 5) & 3;
        if (op & 0x10) {
            if (rm == 15) value += 4;
            if (rn == 15) first += 4;
            operand = shift_register(value, type, cpu->r[(op >> 8) & 15] & 0xFF, carry, &shifter_carry);
        } else {
            operand = shift_immediate(value, type, (op >> 7) & 31, carry, &shifter_carry);
        }
    }
    uint32_t result = 0;
    bool logical = false, writes = true;
    switch (opcode) {
    case 0x0: result = first & operand; logical = true; break;
    case 0x1: result = first ^ operand; logical = true; break;
    case 0x2: result = add_with_carry(cpu, first, ~operand, 1, set_flags); break;
    case 0x3: result = add_with_carry(cpu, operand, ~first, 1, set_flags); break;
    case 0x4: result = add_with_carry(cpu, first, operand, 0, set_flags); break;
    case 0x5: result = add_with_carry(cpu, first, operand, carry, set_flags); break;
    case 0x6: result = add_with_carry(cpu, first, ~operand, carry, set_flags); break;
    case 0x7: result = add_with_carry(cpu, operand, ~first, carry, set_flags); break;
    case 0x8: result = first & operand; logical = true; writes = false; break;
    case 0x9: result = first ^ operand; logical = true; writes = false; break;
    case 0xA: add_with_carry(cpu, first, ~operand, 1, true); writes = false; break;
    case 0xB: add_with_carry(cpu, first, operand, 0, true); writes = false; break;
    case 0xC: result = first | operand; logical = true; break;
    case 0xD: result = operand; logical = true; break;
    case 0xE: result = first & ~operand; logical = true; break;
    default: result = ~operand; logical = true; break;
    }
    if (set_flags && logical) {
        set_nz(cpu, result);
        cpu->cpsr = (cpu->cpsr & ~ARM_PSR_C) | (shifter_carry << 29);
    }
    if (!writes) return;
    write_register(cpu, rd, result);
    if (rd == 15 && set_flags && has_spsr(cpu)) arm_set_cpsr(cpu, cpu->spsr[bank_of(cpu->cpsr & ARM_PSR_MODE)]);
}

static void multiply(arm_cpu_t *cpu, uint32_t op) {
    int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    uint32_t result = cpu->r[rm] * cpu->r[rs];
    if (op & (1u << 21)) result += cpu->r[rn];
    write_register(cpu, rd, result);
    if (op & (1u << 20)) set_nz(cpu, result);
}

static void multiply_long(arm_cpu_t *cpu, uint32_t op) {
    int high = (op >> 16) & 15, low = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    uint64_t result = (op & (1u << 22))
        ? (uint64_t)((int64_t)(int32_t)cpu->r[rm] * (int32_t)cpu->r[rs])
        : (uint64_t)cpu->r[rm] * cpu->r[rs];
    if (op & (1u << 21)) result += ((uint64_t)cpu->r[high] << 32) | cpu->r[low];
    write_register(cpu, low, (uint32_t)result);
    write_register(cpu, high, (uint32_t)(result >> 32));
    if (op & (1u << 20)) {
        cpu->cpsr &= ~(ARM_PSR_N | ARM_PSR_Z);
        if (result >> 63) cpu->cpsr |= ARM_PSR_N;
        if (!result) cpu->cpsr |= ARM_PSR_Z;
    }
}

static void swap(arm_cpu_t *cpu, uint32_t op) {
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    bool byte = op & (1u << 22);
    uint32_t address = cpu->r[rn], loaded;
    uint32_t value = cpu->r[rm];
    bool user = !privileged(cpu);
    if (byte) {
        if (!load(cpu, address, 1, &loaded, user)) return;
        if (!store(cpu, address, 1, value & 0xFF, user)) return;
    } else {
        if (!load_word(cpu, address, &loaded, user)) return;
        if (!store(cpu, transfer_address(cpu, address, 3), 4, value, user)) return;
    }
    write_register(cpu, rd, loaded);
}

static void status_read(arm_cpu_t *cpu, uint32_t op) {
    int rd = (op >> 12) & 15;
    if (op & (1u << 22)) write_register(cpu, rd, has_spsr(cpu) ? cpu->spsr[bank_of(cpu->cpsr & ARM_PSR_MODE)] : cpu->cpsr);
    else write_register(cpu, rd, cpu->cpsr);
}

static void status_write(arm_cpu_t *cpu, uint32_t op) {
    uint32_t value = (op & (1u << 25)) ? rotate_right(op & 0xFF, ((op >> 8) & 15) * 2) : cpu->r[op & 15];
    uint32_t mask = 0;
    if (op & (1u << 16)) mask |= 0x000000FFu;
    if (op & (1u << 17)) mask |= 0x0000FF00u;
    if (op & (1u << 18)) mask |= 0x00FF0000u;
    if (op & (1u << 19)) mask |= 0xFF000000u;
    if (op & (1u << 22)) {
        if (!has_spsr(cpu)) return;
        uint32_t *spsr = &cpu->spsr[bank_of(cpu->cpsr & ARM_PSR_MODE)];
        *spsr = (*spsr & ~mask) | (value & mask);
        return;
    }
    if (!privileged(cpu)) mask &= 0xFF000000u;
    arm_set_cpsr(cpu, (cpu->cpsr & ~mask) | (value & mask));
}

static void halfword_transfer(arm_cpu_t *cpu, uint32_t op) {
    bool pre = op & (1u << 24), up = op & (1u << 23), immediate = op & (1u << 22);
    bool writeback = op & (1u << 21), loading = op & (1u << 20);
    uint32_t kind = (op >> 5) & 3;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    if (!kind || (!loading && kind != 1)) {
        undefined(cpu, op);
        return;
    }
    uint32_t offset = immediate ? ((op >> 4) & 0xF0) | (op & 0xF) : cpu->r[op & 15];
    uint32_t base = cpu->r[rn];
    uint32_t moved = up ? base + offset : base - offset;
    uint32_t address = pre ? moved : base;
    bool user = !privileged(cpu);
    uint32_t value;
    if (loading) {
        if (kind == 2) {
            if (!load(cpu, address, 1, &value, user)) return;
            value = (uint32_t)(int32_t)(int8_t)value;
        } else {
            if (!load(cpu, transfer_address(cpu, address, 1), 2, &value, user)) return;
            if (kind == 3) value = (uint32_t)(int32_t)(int16_t)value;
        }
    } else {
        uint32_t data = rd == 15 ? cpu->r[15] + 4 : cpu->r[rd];
        if (!store(cpu, transfer_address(cpu, address, 1), 2, data & 0xFFFF, user)) return;
    }
    if (!pre || writeback) write_register(cpu, rn, moved);
    if (loading) write_register(cpu, rd, value);
}

static void single_transfer(arm_cpu_t *cpu, uint32_t op) {
    bool pre = op & (1u << 24), up = op & (1u << 23), byte = op & (1u << 22);
    bool writeback = op & (1u << 21), loading = op & (1u << 20);
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    uint32_t offset;
    if (op & (1u << 25)) {
        uint32_t ignored;
        offset = shift_immediate(cpu->r[op & 15], (op >> 5) & 3, (op >> 7) & 31, (cpu->cpsr >> 29) & 1, &ignored);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = cpu->r[rn];
    uint32_t moved = up ? base + offset : base - offset;
    uint32_t address = pre ? moved : base;
    bool user = !privileged(cpu) || (!pre && writeback);
    uint32_t value;
    if (loading) {
        if (byte) {
            if (!load(cpu, address, 1, &value, user)) return;
        } else if (!load_word(cpu, address, &value, user)) {
            return;
        }
    } else {
        uint32_t data = cpu->r[rd];
        if (byte) {
            if (!store(cpu, address, 1, data & 0xFF, user)) return;
        } else if (!store(cpu, transfer_address(cpu, address, 3), 4, data, user)) {
            return;
        }
    }
    if (!pre || writeback) write_register(cpu, rn, moved);
    if (loading) write_register(cpu, rd, value);
}

static void block_transfer(arm_cpu_t *cpu, uint32_t op) {
    bool pre = op & (1u << 24), up = op & (1u << 23), psr = op & (1u << 22);
    bool writeback = op & (1u << 21), loading = op & (1u << 20);
    int rn = (op >> 16) & 15;
    uint32_t list = op & 0xFFFF;
    uint32_t count = (uint32_t)__builtin_popcount(list);
    uint32_t base = cpu->r[rn];
    uint32_t span = count * 4;
    uint32_t address = transfer_address(cpu, up ? (pre ? base + 4 : base) : (pre ? base - span : base - span + 4), 3);
    uint32_t moved = up ? base + span : base - span;
    bool user_bank = psr && !(loading && (list & 0x8000));
    bool user = !privileged(cpu);
    if (loading) {
        uint32_t values[16];
        uint32_t at = address;
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            if (!load(cpu, at, 4, &values[i], user)) return;
            at += 4;
        }
        if (writeback) write_register(cpu, rn, moved);
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            if (user_bank) set_user_register(cpu, i, values[i]);
            else write_register(cpu, i, values[i]);
        }
        if (psr && (list & 0x8000) && has_spsr(cpu)) arm_set_cpsr(cpu, cpu->spsr[bank_of(cpu->cpsr & ARM_PSR_MODE)]);
        return;
    }
    uint32_t at = address;
    for (int i = 0; i < 16; i++) {
        if (!(list & (1u << i))) continue;
        uint32_t value = user_bank ? user_register(cpu, i) : cpu->r[i];
        if (!store(cpu, at, 4, value, user)) return;
        at += 4;
    }
    if (writeback) write_register(cpu, rn, moved);
}

static void branch(arm_cpu_t *cpu, uint32_t op) {
    uint32_t offset = (uint32_t)((int32_t)(op << 8) >> 6);
    if (op & (1u << 24)) cpu->r[14] = cpu->last_pc + 4;
    cpu->next_pc = cpu->r[15] + offset;
}

static uint32_t cp15_read(arm_cpu_t *cpu, uint32_t crn, uint32_t opc2) {
    switch (crn) {
    case 0: return opc2 == 0 ? SA1110_ID : 0;
    case 1: return cpu->control | 0x70u;
    case 2: return cpu->ttb;
    case 3: return cpu->dacr;
    case 5: return cpu->fsr;
    case 6: return cpu->far;
    case 13: return cpu->pid;
    default: return 0;
    }
}

static void cp15_write(arm_cpu_t *cpu, uint32_t op, uint32_t crn, uint32_t crm, uint32_t opc2, uint32_t value) {
    switch (crn) {
    case 1:
        if ((cpu->control ^ value) & ARM_CONTROL_M) {
            cpu->prefetched_control = cpu->control;
            cpu->prefetched = 2;
        }
        cpu->control = value;
        arm_flush_translations(cpu);
        break;
    case 2:
        cpu->ttb = value;
        break;
    case 3:
        cpu->dacr = value;
        arm_flush_translations(cpu);
        break;
    case 5:
        cpu->fsr = value;
        break;
    case 6:
        cpu->far = value;
        break;
    case 8:
        if (crm == 7 || crm == 5) tlb_flush(&cpu->instruction_tlb);
        if (crm == 7 || (crm == 6 && opc2 == 0)) tlb_flush(&cpu->data_tlb);
        if (crm == 6 && opc2 == 1) {
            int slot = tlb_find(&cpu->data_tlb, modified_address(cpu, value));
            if (slot >= 0) cpu->data_tlb.entries[slot].serial = 0;
        }
        break;
    case 13:
        cpu->pid = value & 0xFE000000u;
        break;
    case 15:
        if (crm == 8 && opc2 == 2) cpu->waiting = true;
        break;
    default:
        break;
    }
    if (cpu->on_cp15) cpu->on_cp15(cpu->hook_context, op, value);
}

static void coprocessor_register(arm_cpu_t *cpu, uint32_t op) {
    if (((op >> 8) & 15) != 15 || !privileged(cpu)) {
        undefined(cpu, op);
        return;
    }
    uint32_t crn = (op >> 16) & 15, crm = op & 15, opc2 = (op >> 5) & 7;
    int rd = (op >> 12) & 15;
    if (op & (1u << 20)) {
        uint32_t value = cp15_read(cpu, crn, opc2);
        if (rd == 15) cpu->cpsr = (cpu->cpsr & 0x0FFFFFFFu) | (value & 0xF0000000u);
        else cpu->r[rd] = value;
    } else {
        cp15_write(cpu, op, crn, crm, opc2, rd == 15 ? cpu->r[15] + 4 : cpu->r[rd]);
    }
}

static void execute(arm_cpu_t *cpu, uint32_t op) {
    if (!((condition_table[op >> 28] >> (cpu->cpsr >> 28)) & 1)) return;
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FC000F0u) == 0x00000090u) multiply(cpu, op);
        else if ((op & 0x0F8000F0u) == 0x00800090u) multiply_long(cpu, op);
        else if ((op & 0x0FB00FF0u) == 0x01000090u) swap(cpu, op);
        else if ((op & 0x0E000090u) == 0x00000090u) halfword_transfer(cpu, op);
        else if ((op & 0x0FBF0FFFu) == 0x010F0000u) status_read(cpu, op);
        else if ((op & 0x0FB0FFF0u) == 0x0120F000u) status_write(cpu, op);
        else if ((op & 0x01900000u) == 0x01000000u) undefined(cpu, op);
        else data_processing(cpu, op);
        break;
    case 1:
        if ((op & 0x0FB0F000u) == 0x0320F000u) status_write(cpu, op);
        else if ((op & 0x01900000u) == 0x01000000u) undefined(cpu, op);
        else data_processing(cpu, op);
        break;
    case 2:
        single_transfer(cpu, op);
        break;
    case 3:
        if (op & 0x10) undefined(cpu, op);
        else single_transfer(cpu, op);
        break;
    case 4:
        block_transfer(cpu, op);
        break;
    case 5:
        branch(cpu, op);
        break;
    case 6:
        undefined(cpu, op);
        break;
    default:
        if (op & (1u << 24)) {
            if (!cpu->on_swi || !cpu->on_swi(cpu->bus.context, op & 0x00FFFFFFu))
                raise_exception(cpu, ARM_MODE_SVC, VECTOR_SWI, cpu->last_pc + 4);
        }
        else if (op & 0x10) coprocessor_register(cpu, op);
        else undefined(cpu, op);
        break;
    }
}

static bool fetch_prefetched(arm_cpu_t *cpu, uint32_t pc, uint32_t *op) {
    uint32_t control = cpu->control, pa, fault;
    cpu->control = cpu->prefetched_control;
    bool translated = walk(cpu, modified_address(cpu, pc), false, !privileged(cpu), &pa, &fault);
    cpu->control = control;
    cpu->prefetched--;
    if (translated) {
        uint8_t *host = cpu->bus.page(cpu->bus.context, pa & ~BLOCK_MASK, false);
        if (host) {
            memcpy(op, host + (pa & BLOCK_MASK), 4);
            return true;
        }
        if (cpu->bus.read(cpu->bus.context, pa, 4, op)) return true;
    }
    cpu->last_pc = pc;
    raise_exception(cpu, ARM_MODE_ABT, VECTOR_PREFETCH, pc + 4);
    cpu->r[15] = cpu->next_pc;
    return false;
}

static bool fetch(arm_cpu_t *cpu, uint32_t pc, uint32_t *op) {
    if (cpu->prefetched) return fetch_prefetched(cpu, pc, op);
    uint32_t mva = modified_address(cpu, pc), fault = 0;
    bool user = !privileged(cpu);
    arm_page_cache_t *slot = lookup(cpu, ACCESS_FETCH, mva, user, &fault);
    if (slot) {
        if (slot->host) {
            memcpy(op, slot->host + (mva & BLOCK_MASK), 4);
            return true;
        }
        if (cpu->bus.read(cpu->bus.context, slot->pa | (mva & BLOCK_MASK), 4, op)) return true;
    }
    cpu->last_pc = pc;
    raise_exception(cpu, ARM_MODE_ABT, VECTOR_PREFETCH, pc + 4);
    cpu->r[15] = cpu->next_pc;
    return false;
}

void arm_run(arm_cpu_t *cpu, uint64_t until_cycle) {
    cpu->yield = false;
    while (cpu->cycles < until_cycle && !cpu->yield) {
        if (cpu->waiting) {
            if (!cpu->irq_line && !cpu->fiq_line) {
                cpu->cycles = until_cycle;
                break;
            }
            cpu->waiting = false;
        }
        cpu->cycles++;
        uint32_t pc = cpu->r[15];
        if (cpu->fiq_line && !(cpu->cpsr & ARM_PSR_F)) {
            raise_exception(cpu, ARM_MODE_FIQ, VECTOR_FIQ, pc + 4);
            cpu->r[15] = cpu->next_pc;
            continue;
        }
        if (cpu->irq_line && !(cpu->cpsr & ARM_PSR_I)) {
            raise_exception(cpu, ARM_MODE_IRQ, VECTOR_IRQ, pc + 4);
            cpu->r[15] = cpu->next_pc;
            continue;
        }
        if (cpu->before && cpu->before(cpu->hook_context, pc)) break;
        if (cpu->optimiser_filter) {
            uint32_t bit = (pc >> 2) & 8191u;
            if ((cpu->optimiser_filter[bit >> 3] & (1u << (bit & 7))) &&
                cpu->on_optimiser(cpu->optimiser_context, cpu, pc)) continue;
        }
        uint32_t op;
        if (!fetch(cpu, pc, &op)) continue;
        cpu->last_pc = pc;
        cpu->next_pc = pc + 4;
        cpu->r[15] = pc + 8;
        execute(cpu, op);
        if (cpu->prefetched && cpu->next_pc != pc + 4) cpu->prefetched = 0;
        cpu->r[15] = cpu->next_pc;
        cpu->instructions++;
    }
}

bool arm_read_virtual(arm_cpu_t *cpu, uint32_t va, int size, uint32_t *value) {
    uint32_t pa;
    if (!arm_translate(cpu, va, false, false, &pa, NULL)) return false;
    uint8_t *host = cpu->bus.page(cpu->bus.context, pa & ~BLOCK_MASK, false);
    if (!host) return cpu->bus.read(cpu->bus.context, pa, size, value);
    *value = 0;
    memcpy(value, host + (pa & BLOCK_MASK), (size_t)size);
    return true;
}

bool arm_read_instruction_virtual(arm_cpu_t *cpu, uint32_t va, uint32_t *value) {
    uint32_t mva = modified_address(cpu, va), fault = 0;
    bool user = !privileged(cpu);
    arm_page_cache_t *slot = lookup(cpu, ACCESS_FETCH, mva, user, &fault);
    if (!slot || !slot->host) return false;
    memcpy(value, slot->host + (mva & BLOCK_MASK), 4);
    return true;
}

bool arm_write_virtual(arm_cpu_t *cpu, uint32_t va, int size, uint32_t value) {
    uint32_t pa;
    if (!arm_translate(cpu, va, true, false, &pa, NULL)) return false;
    uint8_t *host = cpu->bus.page(cpu->bus.context, pa & ~BLOCK_MASK, true);
    if (!host) return cpu->bus.write(cpu->bus.context, pa, size, value);
    memcpy(host + (pa & BLOCK_MASK), &value, (size_t)size);
    return true;
}
