#include "core/ce.h"

#include <string.h>
#include <strings.h>

#define KERNEL_BASE         0x80000000u
#define KDATA_VA            0xFFFFC800u
#define KDATA_SECTIONS      0xA0u
#define KINFO               0x300u
#define KINX_PROCARRAY      0
#define KINX_PAGESIZE       1
#define KINX_PFN_MASK       3
#define KINX_MODULES        9
#define KINX_KDATA_ADDR     13
#define PROCESS_VM_BASE     0x0Cu
#define PROCESS_NAME        0x20u
#define PROCESS_STRIDE_MIN  0x80u
#define PROCESS_STRIDE_MAX  0x100u
#define BLOCK_PAGES         0x0Cu
#define BLOCK_RESERVED      1u
#define PAGE_TYPE           0x003u
#define PAGE_ACCESS         0xFF0u
#define PHYSICAL_BLOCK      0x400u
#define SLOT_BASE(process)  ((uint32_t)((process) + 1) * CE_SLOT_SIZE)

typedef struct {
    uint32_t self;
    uint32_t next;
    uint32_t name;
    uint32_t in_use;
    uint32_t base;
    uint32_t entry;
} module_layout_t;

static const module_layout_t MODULE_LAYOUT = { 0x00, 0x04, 0x08, 0x0C, 0x54, 0x60 };

void ce_init(ce_t *ce, machine_t *machine) {
    memset(ce, 0, sizeof *ce);
    ce->machine = machine;
}

static bool physical_copy(ce_t *ce, uint32_t pa, uint8_t *data, uint32_t length, bool write) {
    arm_bus_t *bus = &ce->machine->cpu.bus;
    while (length) {
        uint32_t chunk = PHYSICAL_BLOCK - (pa & (PHYSICAL_BLOCK - 1));
        if (chunk > length) chunk = length;
        uint8_t *host = bus->page(bus->context, pa & ~(PHYSICAL_BLOCK - 1), write);
        if (!host) return false;
        if (write) memcpy(host + (pa & (PHYSICAL_BLOCK - 1)), data, chunk);
        else memcpy(data, host + (pa & (PHYSICAL_BLOCK - 1)), chunk);
        pa += chunk;
        data += chunk;
        length -= chunk;
    }
    return true;
}

static bool physical_word(ce_t *ce, uint32_t pa, uint32_t *value) {
    uint8_t bytes[4];
    if (!physical_copy(ce, pa, bytes, 4, false)) return false;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return true;
}

static bool table_translate(ce_t *ce, uint32_t va, uint32_t *pa) {
    const arm_cpu_t *cpu = &ce->machine->cpu;
    uint32_t first, second;
    if (!(cpu->control & ARM_CONTROL_M)) {
        *pa = va;
        return true;
    }
    if (!physical_word(ce, (cpu->ttb & 0xFFFFC000u) | (va >> 20) << 2, &first)) return false;
    switch (first & 3) {
    case 2:
        *pa = (first & 0xFFF00000u) | (va & 0x000FFFFFu);
        return true;
    case 1:
        if (!physical_word(ce, (first & 0xFFFFFC00u) | ((va >> 10) & 0x3FCu), &second)) return false;
        break;
    case 3:
        if (!physical_word(ce, (first & 0xFFFFF000u) | ((va >> 8) & 0xFFCu), &second)) return false;
        break;
    default:
        return false;
    }
    switch (second & 3) {
    case 1:
        *pa = (second & 0xFFFF0000u) | (va & 0x0000FFFFu);
        return true;
    case 2:
        *pa = (second & 0xFFFFF000u) | (va & 0x00000FFFu);
        return true;
    case 3:
        *pa = (second & 0xFFFFFC00u) | (va & 0x000003FFu);
        return true;
    default:
        return false;
    }
}

static bool kernel_word(ce_t *ce, uint32_t va, uint32_t *value) {
    uint32_t pa;
    if (va < KERNEL_BASE || !table_translate(ce, va, &pa)) return false;
    return physical_word(ce, pa, value);
}

static bool kinfo(ce_t *ce, int index, uint32_t *value) {
    return physical_word(ce, ce->kdata + KINFO + (uint32_t)index * 4, value);
}

static bool kdata_valid(ce_t *ce, uint32_t pa) {
    uint32_t self, page_size;
    return physical_word(ce, pa + KINFO + KINX_KDATA_ADDR * 4, &self) && self == KDATA_VA &&
           physical_word(ce, pa + KINFO + KINX_PAGESIZE * 4, &page_size) && (page_size == 0x400 || page_size == 0x1000);
}

static bool find_kdata(ce_t *ce) {
    uint32_t pa;
    if (!table_translate(ce, KDATA_VA, &pa) || !kdata_valid(ce, pa)) return false;
    ce->kdata = pa;
    return true;
}

static bool find_process_stride(ce_t *ce) {
    uint32_t array;
    if (!kinfo(ce, KINX_PROCARRAY, &array) || array < KERNEL_BASE) return false;
    for (uint32_t stride = PROCESS_STRIDE_MIN; stride <= PROCESS_STRIDE_MAX; stride += 4) {
        int used = 0;
        bool consistent = true;
        for (int process = 0; process < CE_PROCESS_MAX && consistent; process++) {
            uint32_t vm_base;
            if (!kernel_word(ce, array + (uint32_t)process * stride + PROCESS_VM_BASE, &vm_base)) consistent = false;
            else if (vm_base == SLOT_BASE(process)) used++;
            else if (vm_base || !process) consistent = false;
        }
        if (consistent && used >= 3) {
            ce->process_array = array;
            ce->process_stride = stride;
            return true;
        }
    }
    return false;
}

static bool kdata_ready(ce_t *ce) {
    if (ce->kdata && kdata_valid(ce, ce->kdata)) return true;
    ce->kdata = 0;
    ce->process_stride = 0;
    if (!find_kdata(ce)) return false;
    uint32_t page_size, pfn_mask;
    ce->page_size = kinfo(ce, KINX_PAGESIZE, &page_size) ? page_size : 0x1000;
    ce->pfn_mask = kinfo(ce, KINX_PFN_MASK, &pfn_mask) ? pfn_mask : ~(ce->page_size - 1);
    return true;
}

bool ce_ready(ce_t *ce) {
    if (!kdata_ready(ce)) return false;
    return ce->process_stride || find_process_stride(ce);
}

int ce_current_process(ce_t *ce) {
    uint32_t current, section;
    if (ce_ready(ce) && physical_word(ce, ce->kdata + KDATA_SECTIONS, &current) && current) {
        for (int slot = 1; slot <= CE_PROCESS_MAX; slot++) {
            if (physical_word(ce, ce->kdata + KDATA_SECTIONS + (uint32_t)slot * 4, &section) && section == current) return slot - 1;
        }
    }
    uint32_t pid = ce->machine->cpu.pid;
    return pid ? (int)(pid / CE_SLOT_SIZE) - 1 : -1;
}

static bool section_translate(ce_t *ce, uint32_t va, bool write, uint32_t *pa) {
    uint32_t section, block, page;
    if (!kdata_ready(ce)) return false;
    if (!physical_word(ce, ce->kdata + KDATA_SECTIONS + (va >> 25) * 4, &section) || !section) return false;
    if (!kernel_word(ce, section + ((va >> 16) & 0x1FF) * 4, &block) || block <= BLOCK_RESERVED) return false;
    uint32_t index = (va & 0xFFFFu) / ce->page_size;
    if (!kernel_word(ce, block + BLOCK_PAGES + index * 4, &page) || !(page & PAGE_TYPE)) return false;
    if (write && (page & PAGE_ACCESS) != PAGE_ACCESS) return false;
    *pa = (page & ce->pfn_mask) | (va & (ce->page_size - 1));
    return true;
}

bool ce_translate(ce_t *ce, uint32_t va, int process, bool write, uint32_t *pa) {
    if (va >= KERNEL_BASE) return table_translate(ce, va, pa);
    if (va < CE_SLOT_SIZE) {
        int current = ce_current_process(ce);
        if (process == CE_CURRENT) process = current;
        if (process >= 0) va += SLOT_BASE(process);
        else va |= ce->machine->cpu.pid;
    }
    if (section_translate(ce, va, write, pa)) return true;
    return table_translate(ce, va, pa);
}

static bool copy(ce_t *ce, uint32_t va, int process, uint8_t *data, uint32_t length, bool write) {
    uint32_t page = ce->page_size ? ce->page_size : 0x1000;
    while (length) {
        uint32_t pa, chunk = page - (va & (page - 1));
        if (chunk > length) chunk = length;
        if (!ce_translate(ce, va, process, false, &pa) || !physical_copy(ce, pa, data, chunk, write)) return false;
        va += chunk;
        data += chunk;
        length -= chunk;
    }
    return true;
}

bool ce_read(ce_t *ce, uint32_t va, int process, uint8_t *data, uint32_t length) {
    return copy(ce, va, process, data, length, false);
}

bool ce_write(ce_t *ce, uint32_t va, int process, const uint8_t *data, uint32_t length) {
    return copy(ce, va, process, (uint8_t *)data, length, true);
}

bool ce_read_word(ce_t *ce, uint32_t va, int process, uint32_t *value) {
    uint8_t bytes[4];
    if (!ce_read(ce, va, process, bytes, 4)) return false;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return true;
}

static void read_name(ce_t *ce, uint32_t pointer, int process, char *name, size_t size) {
    uint8_t text[CE_NAME_MAX * 2];
    uint32_t available = 0;
    name[0] = 0;
    while (available < sizeof text && ce_read(ce, pointer + available, process, text + available, 1)) available++;
    bool wide = available >= 2 && text[1] == 0;
    size_t length = 0;
    for (uint32_t i = 0; i < available && length + 1 < size; i += wide ? 2 : 1) {
        if (!text[i] || (wide && (i + 1 >= available || text[i + 1]))) break;
        name[length++] = (char)text[i];
    }
    name[length] = 0;
}

bool ce_process_name(ce_t *ce, int process, char *name, size_t size) {
    name[0] = 0;
    if (process < 0 || process >= CE_PROCESS_MAX || !ce_ready(ce)) return false;
    uint32_t entry = ce->process_array + (uint32_t)process * ce->process_stride, vm_base, pointer;
    if (!kernel_word(ce, entry + PROCESS_VM_BASE, &vm_base) || vm_base != SLOT_BASE(process)) return false;
    if (!kernel_word(ce, entry + PROCESS_NAME, &pointer) || !pointer) return false;
    read_name(ce, pointer, process, name, size);
    return name[0] != 0;
}

int ce_find_process(ce_t *ce, const char *name) {
    for (int process = 0; process < CE_PROCESS_MAX; process++) {
        char found[CE_NAME_MAX];
        if (ce_process_name(ce, process, found, sizeof found) && !strcasecmp(found, name)) return process;
    }
    return -1;
}

static bool module_valid(ce_t *ce, uint32_t module) {
    uint32_t self;
    if (module < KERNEL_BASE || (module & 3)) return false;
    return kernel_word(ce, module + MODULE_LAYOUT.self, &self) && self == module;
}

bool ce_first_module(ce_t *ce, uint32_t *module) {
    return ce_ready(ce) && kinfo(ce, KINX_MODULES, module) && module_valid(ce, *module);
}

bool ce_module_entry(ce_t *ce, uint32_t module, uint32_t *entry) {
    return module_valid(ce, module) && kernel_word(ce, module + MODULE_LAYOUT.entry, entry) && *entry && *entry != 0xFFFFFFFFu;
}

int ce_modules(ce_t *ce, ce_module_t *modules, int max) {
    uint32_t module;
    if (!ce_first_module(ce, &module)) return 0;
    int count = 0;
    for (int guard = 0; module && guard < CE_MODULE_MAX * 4 && count < max && module_valid(ce, module); guard++) {
        uint32_t name, base, in_use, entry;
        ce_module_t *found = &modules[count];
        if (kernel_word(ce, module + MODULE_LAYOUT.name, &name) && kernel_word(ce, module + MODULE_LAYOUT.base, &base) &&
            kernel_word(ce, module + MODULE_LAYOUT.in_use, &in_use) && kernel_word(ce, module + MODULE_LAYOUT.entry, &entry)) {
            read_name(ce, name, 0, found->name, sizeof found->name);
            found->base = base & (CE_SLOT_SIZE - 1);
            found->in_use = in_use;
            found->entry = entry;
            if (found->name[0]) count++;
        }
        if (!kernel_word(ce, module + MODULE_LAYOUT.next, &module)) break;
    }
    return count;
}
