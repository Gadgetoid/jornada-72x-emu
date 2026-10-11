#include "core/machine.h"

#include <stdlib.h>
#include <string.h>

#define DRAM_BASE   0xC0000000u
#define DRAM_WINDOW 0x08000000u
#define ZERO_BASE   0xE0000000u
#define ZERO_WINDOW 0x08000000u
#define SOC_BASE    0x80000000u
#define SOC_END     0xC0000000u
#define DEBUG_BOARD_BASE 0x18000000u
#define DEBUG_BOARD_SIZE 0x04000000u

#define PEN_SAMPLE_HZ 100
#define GPIO_KEYBOARD 0
#define GPIO_PEN 9
#define GPIO_MCU_READY 10
#define GPIO_MCU_SELECT 25
#define GPIO_SA1111_INT 1
#define GPIO_SA1111_RESET 20
#define SA1111_BLOCK_B 1
#define SA1111_EEPROM_SELECT 0

static void note_unknown(machine_t *machine, uint32_t pa, int size, bool write, uint32_t value) {
    for (int i = 0; i < machine->unknown_count; i++) {
        machine_unknown_t *entry = &machine->unknown[i];
        if (entry->pa == pa && entry->write == write) {
            entry->count++;
            entry->value = value;
            entry->pc = machine->cpu.last_pc;
            return;
        }
    }
    if (machine->log)
        fprintf(machine->log, "%s %08x size %d value %08x pc %08x\n", write ? "write" : "read ", pa, size, value, machine->cpu.last_pc);
    if (machine->unknown_count == MACHINE_UNKNOWN_MAX) return;
    machine->unknown[machine->unknown_count++] = (machine_unknown_t){
        .pa = pa, .pc = machine->cpu.last_pc, .value = value, .count = 1, .write = write, .size = (uint8_t)size,
    };
}

static void update_lines(machine_t *machine) {
    machine->cpu.irq_line = sa1110_irq(&machine->soc);
    machine->cpu.fiq_line = sa1110_fiq(&machine->soc);
}

static uint8_t *bus_page(void *context, uint32_t pa, bool write) {
    machine_t *machine = context;
    if (pa < MACHINE_ROM_SIZE) return write ? NULL : machine->rom + pa;
    if (pa - DRAM_BASE < DRAM_WINDOW) {
        if (pa - DRAM_BASE >= machine->dram_size && machine->log) fprintf(machine->log, "dram alias %08x pc %08x\n", pa, machine->cpu.last_pc);
        return machine->dram + ((pa - DRAM_BASE) & (machine->dram_size - 1));
    }
    if (pa - ZERO_BASE < ZERO_WINDOW && !write) return machine->zero;
    if (pa - SED1356_MEMORY_BASE < SED1356_MEMORY_SIZE) return machine->display.memory + (pa - SED1356_MEMORY_BASE);
    return NULL;
}

static bool bus_read(void *context, uint32_t pa, int size, uint32_t *value) {
    machine_t *machine = context;
    if (pa >= SOC_BASE && pa < SOC_END) {
        uint32_t word = 0;
        bool known = sa1110_read(&machine->soc, machine->cpu.cycles, pa & ~3u, &word);
        *value = size == 4 ? word : (word >> ((pa & 3) * 8)) & (size == 2 ? 0xFFFFu : 0xFFu);
        if (pa == 0x90000010u)
            optimiser_poll(&machine->optimiser, pa, machine->cpu.last_pc, *value, &machine->soc);
        if (!known) note_unknown(machine, pa, size, false, 0);
        update_lines(machine);
        return true;
    }
    if (pa - ZERO_BASE < ZERO_WINDOW || pa - DEBUG_BOARD_BASE < DEBUG_BOARD_SIZE) {
        *value = 0;
        return true;
    }
    if (pa - SA1111_BASE < SA1111_SIZE) {
        uint32_t word = 0;
        bool known = sa1111_read(&machine->sa1111, (pa - SA1111_BASE) & ~3u, &word);
        *value = size == 4 ? word : (word >> ((pa & 3) * 8)) & (size == 2 ? 0xFFFFu : 0xFFu);
        if (!known) note_unknown(machine, pa, size, false, 0);
        update_lines(machine);
        return true;
    }
    if (pa - SED1356_REGISTER_BASE < SED1356_REGISTER_SIZE) {
        *value = sed1356_read(&machine->display, pa - SED1356_REGISTER_BASE, size);
        return true;
    }
    *value = 0;
    note_unknown(machine, pa, size, false, 0);
    return true;
}

static bool bus_write(void *context, uint32_t pa, int size, uint32_t value) {
    machine_t *machine = context;
    if (pa >= SOC_BASE && pa < SOC_END) {
        if (!sa1110_write(&machine->soc, machine->cpu.cycles, pa & ~3u, value)) note_unknown(machine, pa, size, true, value);
        update_lines(machine);
        machine->cpu.yield = true;
        return true;
    }
    if (pa - ZERO_BASE < ZERO_WINDOW || pa - DEBUG_BOARD_BASE < DEBUG_BOARD_SIZE) return true;
    if (pa - SA1111_BASE < SA1111_SIZE) {
        uint32_t offset = pa - SA1111_BASE;
        if (machine->log && ((offset >= 0x600 && offset < 0x700) || (offset >= 0x200 && offset < 0x224)))
            fprintf(machine->log, "sa1111 write %03x = %08x pc %08x\n", offset, value, machine->cpu.last_pc);
        if (!sa1111_write(&machine->sa1111, (pa - SA1111_BASE) & ~3u, value)) note_unknown(machine, pa, size, true, value);
        update_lines(machine);
        machine->cpu.yield = true;
        return true;
    }
    if (pa - SED1356_REGISTER_BASE < SED1356_REGISTER_SIZE) {
        sed1356_write(&machine->display, pa - SED1356_REGISTER_BASE, size, value);
        return true;
    }
    note_unknown(machine, pa, size, true, value);
    return true;
}

static void soc_gpio_changed(void *context, uint32_t level, uint32_t direction) {
    machine_t *machine = context;
    if ((direction >> GPIO_MCU_SELECT) & 1) {
        bool selected = !((level >> GPIO_MCU_SELECT) & 1);
        if (machine->log && selected != machine->mcu.selected) fprintf(machine->log, "mcu %s\n", selected ? "select" : "release");
        mcu_select(&machine->mcu, selected);
    }
    bool sa1111_running = (direction >> GPIO_SA1111_RESET) & 1 ? (level >> GPIO_SA1111_RESET) & 1 : true;
    if (!sa1111_running) sa1111_reset(&machine->sa1111);
}

static uint8_t reverse_bits(uint8_t value) {
    value = (uint8_t)(((value & 0xF0) >> 4) | ((value & 0x0F) << 4));
    value = (uint8_t)(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
    return (uint8_t)(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
}

static void update_mcu_lines(machine_t *machine) {
    sa1110_set_gpio_input(&machine->soc, GPIO_KEYBOARD, !mcu_keyboard_pending(&machine->mcu));
    sa1110_set_gpio_input(&machine->soc, GPIO_PEN, !machine->mcu.pen_down);
    sa1110_set_gpio_input(&machine->soc, GPIO_MCU_READY, false);
}

static uint16_t soc_ssp_exchange(void *context, uint16_t value) {
    machine_t *machine = context;
    uint8_t sent = reverse_bits((uint8_t)(value >> 8));
    uint8_t answer = mcu_exchange(&machine->mcu, sent);
    if (machine->log) fprintf(machine->log, "mcu %02x -> %02x pc %08x pid %08x\n", sent, answer, machine->cpu.last_pc, machine->cpu.pid);
    uint8_t reply = reverse_bits(answer);
    update_mcu_lines(machine);
    return reply;
}

static void wake(machine_t *machine) {
    sa1110_t *soc = &machine->soc;
    uint32_t pspr = soc->pspr, pwer = soc->pwer, pgsr = soc->pgsr, rtar = soc->rtar, rttr = soc->rttr;
    machine_reset(machine);
    soc->pspr = pspr;
    soc->pwer = pwer;
    soc->pgsr = pgsr;
    soc->rtar = rtar;
    soc->rttr = rttr;
    soc->rcsr = SA1110_RCSR_SMR;
    soc->pssr = SA1110_PSSR_SSS;
}

void machine_key(machine_t *machine, uint8_t scancode, bool down) {
    if (machine->powered_off && down && scancode == MACHINE_POWER_KEY && (machine->soc.pwer & 1u)) wake(machine);
    mcu_key(&machine->mcu, scancode, down);
    update_mcu_lines(machine);
}

void machine_pen(machine_t *machine, bool down, uint16_t x, uint16_t y) {
    if (down && !machine->mcu.pen_down) machine->pen_next_sample = machine->cpu.cycles + MACHINE_CPU_HZ / PEN_SAMPLE_HZ;
    mcu_pen(&machine->mcu, down, x, y);
    update_mcu_lines(machine);
}

void machine_touch(machine_t *machine, bool down, int x, int y) {
    if (x < 0) x = 0;
    if (x > 639) x = 639;
    if (y < 0) y = 0;
    if (y > 239) y = 239;
    machine_pen(machine, down, (uint16_t)(64 + x * 896 / 640), (uint16_t)(64 + y * 896 / 240));
}

static void pen_tick(machine_t *machine) {
    if (!machine->mcu.pen_down || machine->cpu.cycles < machine->pen_next_sample) return;
    machine->pen_next_sample += MACHINE_CPU_HZ / PEN_SAMPLE_HZ;
    sa1110_set_gpio_input(&machine->soc, GPIO_PEN, true);
    sa1110_set_gpio_input(&machine->soc, GPIO_PEN, false);
}

static uint8_t sa1111_ssp_exchange(void *context, uint8_t value) {
    machine_t *machine = context;
    uint8_t reply = eeprom_exchange(&machine->eeprom, value);
    if (machine->log) fprintf(machine->log, "eeprom %s %02x -> %02x\n", machine->eeprom.selected ? "sel" : "off", value, reply);
    return reply;
}

static void sa1111_gpio_changed(void *context, int block, uint32_t level) {
    machine_t *machine = context;
    bool output = !((machine->sa1111.gpio_direction[block] >> SA1111_EEPROM_SELECT) & 1);
    if (block == SA1111_BLOCK_B && output) eeprom_select(&machine->eeprom, !((level >> SA1111_EEPROM_SELECT) & 1));
}

static void sa1111_interrupt_changed(void *context, bool level) {
    machine_t *machine = context;
    sa1110_set_gpio_input(&machine->soc, GPIO_SA1111_INT, level);
}

static uint64_t machine_now(void *context) {
    machine_t *machine = context;
    return machine->cpu.cycles;
}

static bool dram_read(void *context, uint32_t pa, void *buffer, uint32_t length) {
    machine_t *machine = context;
    uint8_t *out = buffer;
    for (uint32_t i = 0; i < length; i++) out[i] = machine->dram[(pa + i) & (machine->dram_size - 1)];
    return true;
}

#define GPIO_SERIAL_POWER 8
#define GPIO_SERIAL_DTR   17

static bool dma_read(void *context, uint32_t pa, uint8_t *value) {
    machine_t *machine = context;
    if (pa - DRAM_BASE >= machine->dram_size) return false;
    *value = machine->dram[pa - DRAM_BASE];
    return true;
}

static bool dma_write(void *context, uint32_t pa, uint8_t value) {
    machine_t *machine = context;
    if (pa - DRAM_BASE >= machine->dram_size) return false;
    machine->dram[pa - DRAM_BASE] = value;
    return true;
}

static void uart_transmit(void *context, int port, uint8_t value) {
    machine_t *machine = context;
    if (port != MACHINE_SERIAL_PORT || !machine->serial_connected) return;
    if (machine->serial_tx_write - machine->serial_tx_read >= MACHINE_SERIAL_QUEUE) return;
    machine->serial_tx[machine->serial_tx_write++ % MACHINE_SERIAL_QUEUE] = value;
}

static int uart_receive(void *context, int port) {
    machine_t *machine = context;
    if (port != MACHINE_SERIAL_PORT || machine->serial_rx_read == machine->serial_rx_write) return -1;
    return machine->serial_rx[machine->serial_rx_read++ % MACHINE_SERIAL_QUEUE];
}

void machine_restore_serial_detect(machine_t *machine) {
    uint32_t bit = 1u << MACHINE_GPIO_SERIAL_DETECT;
    if (machine->serial_connected) machine->soc.gpio_input &= ~bit;
    else machine->soc.gpio_input |= bit;
}

void machine_serial_connect(machine_t *machine, bool connected) {
    machine->serial_connected = connected;
    sa1110_set_gpio_input(&machine->soc, MACHINE_GPIO_SERIAL_DETECT, !connected);
    if (connected) return;
    machine->serial_rx_read = machine->serial_rx_write;
    machine->serial_tx_read = machine->serial_tx_write;
}

bool machine_serial_connected(const machine_t *machine) {
    return machine->serial_connected;
}

void machine_serial_send(machine_t *machine, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length && machine_serial_space(machine); i++)
        machine->serial_rx[machine->serial_rx_write++ % MACHINE_SERIAL_QUEUE] = data[i];
    sa1110_uart_wake(&machine->soc, MACHINE_SERIAL_PORT, machine->cpu.cycles);
}

size_t machine_serial_take(machine_t *machine, uint8_t *out, size_t max) {
    size_t count = 0;
    while (count < max && machine->serial_tx_read != machine->serial_tx_write)
        out[count++] = machine->serial_tx[machine->serial_tx_read++ % MACHINE_SERIAL_QUEUE];
    return count;
}

size_t machine_serial_space(const machine_t *machine) {
    return MACHINE_SERIAL_QUEUE - (machine->serial_rx_write - machine->serial_rx_read);
}

uint32_t machine_serial_baud(const machine_t *machine) {
    return sa1110_uart_baud(&machine->soc, MACHINE_SERIAL_PORT);
}

static bool gpio_output(const sa1110_t *soc, int pin, bool level) {
    uint32_t bit = 1u << pin;
    return (soc->gpdr & bit) && ((soc->gplr_out & bit) != 0) == level;
}

bool machine_serial_dtr(const machine_t *machine) {
    return gpio_output(&machine->soc, GPIO_SERIAL_POWER, true) && gpio_output(&machine->soc, GPIO_SERIAL_DTR, false);
}

uint64_t machine_cycles(const machine_t *machine) {
    return machine->cpu.cycles;
}

static void audio_output(void *context, const int16_t *frames, uint32_t count, uint32_t rate) {
    machine_t *machine = context;
    if (rate != machine->audio_rate) machine->audio_read = machine->audio_write;
    machine->audio_rate = rate;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t slot = machine->audio_write % MACHINE_AUDIO_FRAMES;
        machine->audio[slot * 2] = frames[i * 2];
        machine->audio[slot * 2 + 1] = frames[i * 2 + 1];
        machine->audio_write++;
        if (machine->audio_write - machine->audio_read > MACHINE_AUDIO_FRAMES) machine->audio_read++;
    }
}

uint32_t machine_audio_take(machine_t *machine, int16_t *frames, uint32_t max_frames, uint32_t *rate) {
    uint32_t count = 0;
    while (count < max_frames && machine->audio_read != machine->audio_write) {
        uint32_t slot = machine->audio_read % MACHINE_AUDIO_FRAMES;
        frames[count * 2] = machine->audio[slot * 2];
        frames[count * 2 + 1] = machine->audio[slot * 2 + 1];
        machine->audio_read++;
        count++;
    }
    *rate = machine->audio_rate;
    return count;
}

static void power_off(void *context) {
    machine_t *machine = context;
    machine->powered_off = true;
    machine->cpu.yield = true;
}

#define MAILBOX_FAULT_TRIES 4
#define AGENT_POLL_WINDOW   (MACHINE_CPU_HZ / 2)

static bool mailbox_copy(void *context, uint32_t va, uint8_t *data, uint32_t length, bool write) {
    machine_t *machine = context;
    bool user = (machine->cpu.cpsr & ARM_PSR_MODE) == ARM_MODE_USR;
    uint8_t *host = arm_map_virtual(&machine->cpu, va, write, user);
    if (!host) return false;
    if (write) memcpy(host, data, length);
    else memcpy(data, host, length);
    return true;
}

static bool on_swi(void *context, uint32_t comment) {
    machine_t *machine = context;
    arm_cpu_t *cpu = &machine->cpu;
    uint32_t mode = cpu->cpsr & ARM_PSR_MODE;
    if (comment != MAILBOX_SWI_COMMENT || (mode != ARM_MODE_USR && mode != ARM_MODE_SYS)) return false;
    if (cpu->r[0] == MAILBOX_RECV) machine->mailbox_received_at = cpu->cycles;
    uint32_t fault_va;
    if (mailbox_trap(&machine->mailbox, cpu, mailbox_copy, machine, &fault_va)) {
        machine->mailbox_fault_tries = 0;
        return true;
    }
    if (fault_va != machine->mailbox_fault_va) machine->mailbox_fault_tries = 0;
    machine->mailbox_fault_va = fault_va;
    if (++machine->mailbox_fault_tries > MAILBOX_FAULT_TRIES) {
        machine->mailbox_fault_tries = 0;
        cpu->r[0] = (uint32_t)-1;
        return true;
    }
    arm_raise_data_abort(cpu, fault_va, cpu->r[0] == MAILBOX_RECV);
    return true;
}

bool machine_agent_running(const machine_t *machine) {
    return machine->mailbox_received_at && machine->cpu.cycles - machine->mailbox_received_at < AGENT_POLL_WINDOW;
}

bool machine_init(machine_t *machine, const char *rom_path, const char *eeprom_path) {
    memset(machine, 0, sizeof *machine);
    machine->rom = malloc(MACHINE_ROM_SIZE);
    machine->dram_size = MACHINE_DRAM_SIZE;
    machine->dram = calloc(1, machine->dram_size);
    if (!machine->rom || !machine->dram) return false;
    memset(machine->rom, 0xFF, MACHINE_ROM_SIZE);
    FILE *file = fopen(rom_path, "rb");
    if (!file) return false;
    size_t loaded = fread(machine->rom, 1, MACHINE_ROM_SIZE, file);
    fclose(file);
    if (!loaded) return false;
    if (eeprom_path) {
        file = fopen(eeprom_path, "rb");
        if (!file) return false;
        loaded = fread(machine->eeprom.data, 1, sizeof machine->eeprom.data, file);
        fclose(file);
        if (loaded != sizeof machine->eeprom.data) return false;
    }
    memset(machine->open_bus, 0xFF, sizeof machine->open_bus);
    machine->cpu.bus = (arm_bus_t){ .context = machine, .read = bus_read, .write = bus_write, .page = bus_page };
    machine->cpu.on_swi = on_swi;
    machine->soc.cpu_hz = MACHINE_CPU_HZ;
    machine->soc.board = (sa1110_board_t){ .context = machine, .gpio_changed = soc_gpio_changed, .ssp_exchange = soc_ssp_exchange,
                                           .power_off = power_off, .dma_read = dma_read, .dma_write = dma_write,
                                           .uart_transmit = uart_transmit, .uart_receive = uart_receive };
    machine->sa1111.board = (sa1111_board_t){
        .context = machine, .ssp_exchange = sa1111_ssp_exchange, .interrupt_changed = sa1111_interrupt_changed,
        .gpio_changed = sa1111_gpio_changed, .now = machine_now, .cpu_hz = MACHINE_CPU_HZ, .dram_read = dram_read,
        .audio = audio_output,
    };
    machine_reset(machine);
    machine_restore_serial_detect(machine);
    optimiser_init(&machine->optimiser, &machine->cpu);
    return true;
}

void machine_set_optimisations(machine_t *machine, bool enabled) {
    optimiser_set_enabled(&machine->optimiser, enabled);
}

void machine_set_speed(machine_t *machine, uint32_t multiplier) {
    machine->cpu.speed = multiplier < 1 ? 1 : multiplier > MACHINE_SPEED_MAX ? MACHINE_SPEED_MAX : multiplier;
}

uint32_t machine_speed(const machine_t *machine) {
    return machine->cpu.speed ? machine->cpu.speed : 1;
}

void machine_free(machine_t *machine) {
    mailbox_clear(&machine->mailbox);
    free(machine->rom);
    free(machine->dram);
}

void machine_reset(machine_t *machine) {
    arm_reset(&machine->cpu);
    sa1110_reset(&machine->soc, machine->cpu.cycles);
    sa1111_reset(&machine->sa1111);
    sed1356_reset(&machine->display);
    mcu_reset(&machine->mcu);
    update_mcu_lines(machine);
    machine->powered_off = false;
    machine->reset_requested = false;
    mailbox_clear(&machine->mailbox);
    machine->mailbox_received_at = 0;
}

void machine_soft_reset(machine_t *machine) {
    machine_reset(machine);
    machine->soc.rcsr = SA1110_RCSR_SWR;
}

void machine_run(machine_t *machine, uint64_t cycles) {
    arm_cpu_t *cpu = &machine->cpu;
    uint64_t end = cpu->cycles + cycles;
    cpu->stopped = false;
    if (machine->powered_off) {
        cpu->cycles = end;
        sa1110_advance(&machine->soc, cpu->cycles);
        return;
    }
    while (cpu->cycles < end && !machine->powered_off && !cpu->stopped) {
        sa1110_advance(&machine->soc, cpu->cycles);
        sa1111_advance(&machine->sa1111, cpu->cycles);
        pen_tick(machine);
        update_lines(machine);
        if (cpu->waiting && sa1110_wakes_idle(&machine->soc)) cpu->waiting = false;
        uint64_t next = sa1110_next_event(&machine->soc, cpu->cycles);
        if (machine->mcu.pen_down && machine->pen_next_sample < next) next = machine->pen_next_sample;
        uint64_t audio_event = sa1111_next_event(&machine->sa1111);
        if (audio_event < next) next = audio_event;
        if (next > end) next = end;
        if (next <= cpu->cycles) next = cpu->cycles + 1;
        optimiser_set_deadline(&machine->optimiser, next);
        arm_run(cpu, next);
        machine->events++;
    }
}

double machine_seconds(const machine_t *machine) {
    return (double)machine->cpu.cycles / MACHINE_CPU_HZ;
}

void machine_print_status(const machine_t *machine, FILE *out) {
    const arm_cpu_t *cpu = &machine->cpu;
    const sa1110_t *soc = &machine->soc;
    fprintf(out, "t %.3fs instructions %llu%s\n", machine_seconds(machine), (unsigned long long)cpu->instructions,
            cpu->waiting ? " waiting" : "");
    fprintf(out, "pc %08x cpsr %08x control %08x ttb %08x dacr %08x pid %08x fsr %03x far %08x\n", cpu->r[15], cpu->cpsr,
            cpu->control, cpu->ttb, cpu->dacr, cpu->pid, cpu->fsr, cpu->far);
    fprintf(out, "gpio level %08x dir %08x rise %08x fall %08x edge %08x alt %08x  ic mask %08x level %08x pending %08x  irq %d fiq %d\n",
            (soc->gplr_out & soc->gpdr) | (soc->gpio_input & ~soc->gpdr), soc->gpdr, soc->grer, soc->gfer, soc->gedr, soc->gafr,
            soc->icmr, soc->iclr, soc->icpr, cpu->irq_line, cpu->fiq_line);
    fprintf(out, "ostimer oscr %08x osmr0 %08x oier %x ossr %x\n", sa1110_oscr(soc, cpu->cycles), soc->osmr[0], soc->oier, soc->ossr);
    for (int i = 0; i < 16; i++) fprintf(out, "r%-2d %08x%s", i, cpu->r[i], i % 4 == 3 ? "\n" : "  ");
    fprintf(out, "mcu selected %d keys %d pen %d\n", machine->mcu.selected, machine->mcu.key_count, machine->mcu.pen_down);
    if (machine->optimiser.enabled) optimiser_print(&machine->optimiser, out);
}
