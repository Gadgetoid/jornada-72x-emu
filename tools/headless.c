#include "core/machine.h"
#include "core/state.h"
#include "util/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    machine_t *machine;
    FILE *trace;
    uint64_t trace_from;
    uint32_t stop_at;
    bool stopped;
    int exceptions;
    uint64_t exception_counts[8];
} session_t;

static session_t session;

#define INPUT_MAX 256

typedef struct {
    double at;
    int kind;
    int code;
    int x, y;
    bool down;
} input_event_t;

static input_event_t inputs[INPUT_MAX];
static int input_count;

static void add_input(input_event_t event) {
    if (input_count < INPUT_MAX) inputs[input_count++] = event;
}


#define PROFILE_SLOTS 4096

typedef struct {
    uint32_t pc;
    uint32_t pid;
    uint64_t count;
} profile_entry_t;

static profile_entry_t profile[PROFILE_SLOTS];
static bool profiling;
static bool profile_user_only;

static void profile_sample(uint32_t pc, uint32_t pid) {
    uint32_t key = pc < 0x02000000u ? pc | pid : pc;
    uint32_t slot = (key >> 2) * 2654435761u % PROFILE_SLOTS;
    for (int probe = 0; probe < PROFILE_SLOTS; probe++) {
        profile_entry_t *entry = &profile[(slot + (uint32_t)probe) % PROFILE_SLOTS];
        if (!entry->count || entry->pc == key) {
            entry->pc = key;
            entry->count++;
            return;
        }
    }
}

static int profile_order(const void *a, const void *b) {
    const profile_entry_t *left = a, *right = b;
    return left->count < right->count ? 1 : left->count > right->count ? -1 : 0;
}

static bool before(void *context, uint32_t pc) {
    (void)context;
    if (profiling && session.machine->cpu.instructions >= session.trace_from && (!profile_user_only || pc < 0x80000000u) && !(session.machine->cpu.instructions & 255)) profile_sample(pc, session.machine->cpu.pid);
    if (session.trace && session.machine->cpu.instructions >= session.trace_from) {
        arm_cpu_t *cpu = &session.machine->cpu;
        fprintf(session.trace, "%08x cpsr %08x r0 %08x r1 %08x r2 %08x r3 %08x sp %08x lr %08x\n",
                pc, cpu->cpsr, cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], cpu->r[13], cpu->r[14]);
    }
    if (session.stop_at && pc == session.stop_at) {
        session.stopped = true;
        return true;
    }
    return false;
}

static void undefined_instruction(void *context, uint32_t pc, uint32_t op) {
    (void)context;
    fprintf(stderr, "undefined %08x at %08x\n", op, pc);
}

static void exception_raised(void *context, uint32_t vector, uint32_t link) {
    (void)context;
    static int shown;
    arm_cpu_t *cpu = &session.machine->cpu;
    session.exception_counts[(vector >> 2) & 7]++;
    if (vector == 0x18 || vector == 0x1C) return;
    if (cpu->instructions >= session.trace_from && shown++ < session.exceptions)
        fprintf(stderr, "exception %02x link %08x cpsr %08x fsr %03x far %08x sp %08x after %llu\n", vector, link, cpu->cpsr,
                cpu->fsr, cpu->far, cpu->r[13], (unsigned long long)cpu->instructions);
}

static bool screenshot(machine_t *machine, const char *path) {
    int width, height;
    sed1356_size(&machine->display, &width, &height);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
        width = 640;
        height = 240;
    }
    uint32_t *pixels = malloc(sizeof *pixels * (size_t)width * (size_t)height);
    if (!pixels) return false;
    sed1356_render(&machine->display, pixels, width, height);
    for (int i = 0; i < width * height; i++) {
        uint32_t rgb = pixels[i];
        pixels[i] = ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
    uint8_t *png;
    size_t length;
    bool ok = png_encode(pixels, width, height, &png, &length);
    free(pixels);
    if (!ok) return false;
    FILE *file = fopen(path, "wb");
    ok = file && fwrite(png, 1, length, file) == length;
    if (file) fclose(file);
    free(png);
    return ok;
}

static void usage(void) {
    fprintf(stderr,
            "usage: headless [options]\n"
            "  --rom=PATH         ROM image (default rom/jornada720.bin)\n"
            "  --eeprom=PATH      configuration EEPROM (default rom/jorn720_eeprom.bin)\n"
            "  --seconds=N        emulated seconds to run (default 5)\n"
            "  --log              print unknown bus accesses as they happen\n"
            "  --profile[=user]   print the most frequent PCs (modified virtual addresses), from --trace-from on\n"
            "  --optimisations    enable native CE routine optimisations (default off)\n"
            "  --stats=SECONDS    print interrupt and instruction counts at this interval\n"
            "  --load=PATH        start from a saved state (times are relative to its start)\n"
            "  --save=PATH        save state at the end\n"
            "  --wav=PATH         record audio output to a WAV file\n"
            "  --backup=N         backup battery ADC reading (default 0x3C0)\n"
            "  --rtc=N            RTC counter at power on\n"
            "  --key=T:SCAN       press and release an MCU scan code at T seconds\n"
            "  --tap=T:X:Y[:HOLD] touch the screen at pixel X,Y at T seconds (default hold 0.2s)\n"
            "  --dump=VA:LEN:PATH write virtual memory (hex VA and length) to a file at the end\n"
            "  --screenshot=PATH  write the LCD as a PNG at the end\n"
            "  --exceptions=N     print the first N exceptions other than interrupts\n"
            "  --trace=PATH       write a per-instruction trace\n"
            "  --trace-from=N     start tracing after N instructions\n"
            "  --stop-at=PC       stop when PC is reached\n");
}

int main(int argc, char **argv) {
    const char *rom = "rom/jornada720.bin";
    const char *eeprom = "rom/jorn720_eeprom.bin";
    double seconds = 5;
    bool log = false;
    bool optimisations = false;
    double stats_every = 0, next_stats = 0;
    uint32_t rtc_start = 0;
    const char *load_path = NULL, *save_path = NULL;
    int backup_reading = -1;
    const char *wav_path = NULL;
    FILE *wav = NULL;
    uint32_t wav_frames = 0, wav_rate = 0;
    uint64_t last_irqs = 0, last_instructions = 0;
    const char *screenshot_path = NULL;
    uint32_t dump_address = 0, dump_length = 0;
    const char *dump_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strncmp(arg, "--rom=", 6)) rom = arg + 6;
        else if (!strncmp(arg, "--eeprom=", 9)) eeprom = arg + 9;
        else if (!strncmp(arg, "--seconds=", 10)) seconds = atof(arg + 10);
        else if (!strcmp(arg, "--log")) log = true;
        else if (!strcmp(arg, "--optimisations")) optimisations = true;
        else if (!strcmp(arg, "--profile")) profiling = true;
        else if (!strcmp(arg, "--profile=user")) profiling = profile_user_only = true;
        else if (!strncmp(arg, "--stats=", 8)) stats_every = atof(arg + 8);
        else if (!strncmp(arg, "--load=", 7)) load_path = arg + 7;
        else if (!strncmp(arg, "--wav=", 6)) wav_path = arg + 6;
        else if (!strncmp(arg, "--backup=", 9)) backup_reading = (int)strtol(arg + 9, NULL, 0);
        else if (!strncmp(arg, "--save=", 7)) save_path = arg + 7;
        else if (!strncmp(arg, "--rtc=", 6)) rtc_start = (uint32_t)strtoul(arg + 6, NULL, 0);
        else if (!strncmp(arg, "--key=", 6)) {
            double at;
            int code;
            if (sscanf(arg + 6, "%lf:%i", &at, &code) != 2) {
                usage();
                return 2;
            }
            add_input((input_event_t){ .at = at, .kind = 0, .code = code, .down = true });
            add_input((input_event_t){ .at = at + 0.1, .kind = 0, .code = code, .down = false });
        } else if (!strncmp(arg, "--tap=", 6)) {
            double at, hold = 0.2;
            int x, y;
            if (sscanf(arg + 6, "%lf:%d:%d:%lf", &at, &x, &y, &hold) < 3) {
                usage();
                return 2;
            }
            add_input((input_event_t){ .at = at, .kind = 1, .x = x, .y = y, .down = true });
            add_input((input_event_t){ .at = at + hold, .kind = 1, .x = x, .y = y, .down = false });
        }
        else if (!strncmp(arg, "--dump=", 7)) {
            char *end;
            dump_address = (uint32_t)strtoul(arg + 7, &end, 16);
            dump_length = (uint32_t)strtoul(end + 1, &end, 16);
            dump_path = end + 1;
        }
        else if (!strncmp(arg, "--screenshot=", 13)) screenshot_path = arg + 13;
        else if (!strncmp(arg, "--exceptions=", 13)) session.exceptions = atoi(arg + 13);
        else if (!strncmp(arg, "--trace=", 8)) session.trace = fopen(arg + 8, "w");
        else if (!strncmp(arg, "--trace-from=", 13)) session.trace_from = strtoull(arg + 13, NULL, 0);
        else if (!strncmp(arg, "--stop-at=", 10)) session.stop_at = (uint32_t)strtoul(arg + 10, NULL, 16);
        else {
            usage();
            return 2;
        }
    }
    static machine_t machine;
    if (!machine_init(&machine, rom, eeprom)) {
        fprintf(stderr, "cannot load %s\n", rom);
        return 1;
    }
    session.machine = &machine;
    machine.soc.rcnr = rtc_start;
    uint64_t start_cycles = 0;
    if (load_path) {
        state_result_t loaded = state_load(&machine, load_path, 0);
        if (loaded != STATE_OK) {
            fprintf(stderr, "%s: %s\n", load_path, state_describe(loaded));
            return 1;
        }
        start_cycles = machine.cpu.cycles;
    }
    machine_set_optimisations(&machine, optimisations);
    if (backup_reading >= 0) machine.mcu.backup_battery = (uint16_t)backup_reading;
    if (log) machine.log = stderr;
    machine.cpu.hook_context = &session;
    machine.cpu.on_undefined = undefined_instruction;
    machine.cpu.on_exception = exception_raised;
    if (session.trace || session.stop_at || profiling) machine.cpu.before = before;
    clock_t started = clock();
    uint64_t total = start_cycles + (uint64_t)(seconds * MACHINE_CPU_HZ);
    uint64_t slice = MACHINE_CPU_HZ / 100;
    while (machine.cpu.cycles < total && !machine.powered_off && !session.stopped) {
        double now = (double)(machine.cpu.cycles - start_cycles) / MACHINE_CPU_HZ;
        if (stats_every > 0 && now >= next_stats) {
            sa1110_t *soc = &machine.soc;
            printf("rcnr %08x rtar %08x rtsr %x ", soc->rcnr, soc->rtar, soc->rtsr);
            printf("t %7.1f irqs %6llu instructions %10llu oscr %08x osmr0 %08x oier %x ossr %x icmr %08x%s\n", now,
                   (unsigned long long)(session.exception_counts[6] - last_irqs),
                   (unsigned long long)(machine.cpu.instructions - last_instructions), sa1110_oscr(soc, machine.cpu.cycles),
                   soc->osmr[0], soc->oier, soc->ossr, soc->icmr, machine.cpu.waiting ? " waiting" : "");
            last_irqs = session.exception_counts[6];
            last_instructions = machine.cpu.instructions;
            next_stats = now + stats_every;
        }
        for (int i = 0; i < input_count; i++) {
            input_event_t *event = &inputs[i];
            if (event->at < 0 || event->at > now) continue;
            if (event->kind == 0) machine_key(&machine, (uint8_t)event->code, event->down);
            else machine_touch(&machine, event->down, event->x, event->y);
            event->at = -1;
        }
        uint64_t left = total - machine.cpu.cycles;
        machine_run(&machine, left < slice ? left : slice);
        if (wav_path) {
            static int16_t frames[MACHINE_AUDIO_FRAMES * 2];
            uint32_t rate, count = machine_audio_take(&machine, frames, MACHINE_AUDIO_FRAMES, &rate);
            if (count) {
                if (!wav) {
                    wav = fopen(wav_path, "wb");
                    wav_rate = rate;
                    uint8_t header[44] = { 0 };
                    if (wav) fwrite(header, 1, sizeof header, wav);
                }
                if (wav) fwrite(frames, 4, count, wav);
                wav_frames += count;
            }
        }
    }
    double host = (double)(clock() - started) / CLOCKS_PER_SEC;
    arm_cpu_t *cpu = &machine.cpu;
    printf("emulated %.3fs in %.3fs, %llu instructions (%.1f M/s)%s%s\n", machine_seconds(&machine), host,
           (unsigned long long)cpu->instructions, host > 0 ? cpu->instructions / host / 1e6 : 0,
           session.stopped ? ", stopped" : "", machine.powered_off ? ", powered off" : "");
    machine_print_status(&machine, stdout);
    static const char *names[8] = { "reset", "undefined", "swi", "prefetch", "data", "reserved", "irq", "fiq" };
    printf("exceptions:");
    for (int i = 0; i < 8; i++)
        if (session.exception_counts[i]) printf(" %s %llu", names[i], (unsigned long long)session.exception_counts[i]);
    printf("\n");
    printf("unknown accesses: %d\n", machine.unknown_count);
    for (int i = 0; i < machine.unknown_count; i++) {
        machine_unknown_t *entry = &machine.unknown[i];
        printf("  %s %08x size %d count %u last %08x pc %08x\n", entry->write ? "write" : "read ", entry->pa, entry->size,
               entry->count, entry->value, entry->pc);
    }
    if (profiling) {
        qsort(profile, PROFILE_SLOTS, sizeof profile[0], profile_order);
        uint64_t total_samples = 0;
        for (int i = 0; i < PROFILE_SLOTS; i++) total_samples += profile[i].count;
        for (int i = 0; i < 25 && profile[i].count; i++)
            printf("  %08x %5.1f%%\n", profile[i].pc, 100.0 * (double)profile[i].count / (double)total_samples);
    }
    if (wav) {
        uint32_t data = wav_frames * 4;
        uint8_t header[44] = { 'R', 'I', 'F', 'F' };
        uint32_t riff = data + 36, fmt_size = 16, byte_rate = wav_rate * 4;
        uint16_t format = 1, channels = 2, align = 4, bits = 16;
        memcpy(header + 4, &riff, 4);
        memcpy(header + 8, "WAVEfmt ", 8);
        memcpy(header + 16, &fmt_size, 4);
        memcpy(header + 20, &format, 2);
        memcpy(header + 22, &channels, 2);
        memcpy(header + 24, &wav_rate, 4);
        memcpy(header + 28, &byte_rate, 4);
        memcpy(header + 32, &align, 2);
        memcpy(header + 34, &bits, 2);
        memcpy(header + 36, "data", 4);
        memcpy(header + 40, &data, 4);
        fseek(wav, 0, SEEK_SET);
        fwrite(header, 1, sizeof header, wav);
        fclose(wav);
        printf("audio: %u frames at %u Hz\n", wav_frames, wav_rate);
    }
    if (save_path && !state_save(&machine, save_path, 0)) fprintf(stderr, "cannot save %s\n", save_path);
    if (dump_path) {
        FILE *file = fopen(dump_path, "wb");
        for (uint32_t offset = 0; file && offset < dump_length; offset += 4) {
            uint32_t word = 0;
            arm_read_virtual(&machine.cpu, dump_address + offset, 4, &word);
            fwrite(&word, 4, 1, file);
        }
        if (file) fclose(file);
    }
    if (screenshot_path && !screenshot(&machine, screenshot_path)) fprintf(stderr, "cannot write %s\n", screenshot_path);
    if (session.trace) fclose(session.trace);
    machine_free(&machine);
    return 0;
}
