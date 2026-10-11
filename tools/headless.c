#include "core/machine.h"
#include "core/state.h"
#include "util/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INPUT_MAX 256
#define PROFILE_SLOTS 4096

typedef enum { INPUT_KEY, INPUT_TAP } input_kind_t;

typedef struct {
    double at;
    input_kind_t kind;
    int code;
    int x, y;
    bool down;
} input_event_t;

typedef struct {
    uint32_t pc;
    uint32_t pid;
    uint64_t count;
} profile_entry_t;

typedef struct {
    const char *rom;
    const char *eeprom;
    double seconds;
    bool log;
    bool optimisations;
    bool profiling;
    bool profile_user_only;
    double stats_every;
    uint32_t rtc_start;
    int backup_reading;
    const char *load_path;
    const char *save_path;
    const char *wav_path;
    const char *screenshot_path;
    const char *dump_path;
    uint32_t dump_address;
    uint32_t dump_length;
    int exceptions;
    uint64_t trace_from;
    uint32_t stop_at;
} options_t;

typedef struct {
    options_t options;
    machine_t machine;
    FILE *trace;
    bool stopped;
    int exceptions_shown;
    uint64_t exception_counts[8];
    input_event_t inputs[INPUT_MAX];
    int input_count;
    profile_entry_t profile[PROFILE_SLOTS];
    uint64_t start_cycles;
    double next_stats;
    uint64_t last_irqs;
    uint64_t last_instructions;
    FILE *wav;
    uint32_t wav_frames;
    uint32_t wav_rate;
    int16_t audio_frames[MACHINE_AUDIO_FRAMES * 2];
} headless_t;

static void add_input(headless_t *headless, input_event_t event) {
    if (headless->input_count < INPUT_MAX) headless->inputs[headless->input_count++] = event;
}

static void profile_sample(headless_t *headless, uint32_t pc, uint32_t pid) {
    uint32_t key = pc < 0x02000000u ? pc | pid : pc;
    uint32_t slot = (key >> 2) * 2654435761u % PROFILE_SLOTS;
    for (int probe = 0; probe < PROFILE_SLOTS; probe++) {
        profile_entry_t *entry = &headless->profile[(slot + (uint32_t)probe) % PROFILE_SLOTS];
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
    headless_t *headless = context;
    const options_t *options = &headless->options;
    arm_cpu_t *cpu = &headless->machine.cpu;
    if (options->profiling && cpu->instructions >= options->trace_from && (!options->profile_user_only || pc < 0x80000000u)
        && !(cpu->instructions & 255))
        profile_sample(headless, pc, cpu->pid);
    if (headless->trace && cpu->instructions >= options->trace_from) {
        fprintf(headless->trace, "%08x cpsr %08x r0 %08x r1 %08x r2 %08x r3 %08x sp %08x lr %08x\n",
                pc, cpu->cpsr, cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], cpu->r[13], cpu->r[14]);
    }
    if (options->stop_at && pc == options->stop_at) {
        headless->stopped = true;
        return true;
    }
    return false;
}

static void undefined_instruction(void *context, uint32_t pc, uint32_t op) {
    (void)context;
    fprintf(stderr, "undefined %08x at %08x\n", op, pc);
}

static void exception_raised(void *context, uint32_t vector, uint32_t link) {
    headless_t *headless = context;
    arm_cpu_t *cpu = &headless->machine.cpu;
    headless->exception_counts[(vector >> 2) & 7]++;
    if (vector == 0x18 || vector == 0x1C) return;
    if (cpu->instructions >= headless->options.trace_from && headless->exceptions_shown++ < headless->options.exceptions)
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

static bool parse_options(headless_t *headless, int argc, char **argv) {
    options_t *options = &headless->options;
    *options = (options_t){ .rom = "rom/jornada720.bin", .eeprom = "rom/jorn720_eeprom.bin", .seconds = 5, .backup_reading = -1 };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strncmp(arg, "--rom=", 6)) options->rom = arg + 6;
        else if (!strncmp(arg, "--eeprom=", 9)) options->eeprom = arg + 9;
        else if (!strncmp(arg, "--seconds=", 10)) options->seconds = atof(arg + 10);
        else if (!strcmp(arg, "--log")) options->log = true;
        else if (!strcmp(arg, "--optimisations")) options->optimisations = true;
        else if (!strcmp(arg, "--profile")) options->profiling = true;
        else if (!strcmp(arg, "--profile=user")) options->profiling = options->profile_user_only = true;
        else if (!strncmp(arg, "--stats=", 8)) options->stats_every = atof(arg + 8);
        else if (!strncmp(arg, "--load=", 7)) options->load_path = arg + 7;
        else if (!strncmp(arg, "--wav=", 6)) options->wav_path = arg + 6;
        else if (!strncmp(arg, "--backup=", 9)) options->backup_reading = (int)strtol(arg + 9, NULL, 0);
        else if (!strncmp(arg, "--save=", 7)) options->save_path = arg + 7;
        else if (!strncmp(arg, "--rtc=", 6)) options->rtc_start = (uint32_t)strtoul(arg + 6, NULL, 0);
        else if (!strncmp(arg, "--key=", 6)) {
            double at;
            int code;
            if (sscanf(arg + 6, "%lf:%i", &at, &code) != 2) return false;
            add_input(headless, (input_event_t){ .at = at, .kind = INPUT_KEY, .code = code, .down = true });
            add_input(headless, (input_event_t){ .at = at + 0.1, .kind = INPUT_KEY, .code = code, .down = false });
        } else if (!strncmp(arg, "--tap=", 6)) {
            double at, hold = 0.2;
            int x, y;
            if (sscanf(arg + 6, "%lf:%d:%d:%lf", &at, &x, &y, &hold) < 3) return false;
            add_input(headless, (input_event_t){ .at = at, .kind = INPUT_TAP, .x = x, .y = y, .down = true });
            add_input(headless, (input_event_t){ .at = at + hold, .kind = INPUT_TAP, .x = x, .y = y, .down = false });
        } else if (!strncmp(arg, "--dump=", 7)) {
            char *end;
            options->dump_address = (uint32_t)strtoul(arg + 7, &end, 16);
            options->dump_length = (uint32_t)strtoul(end + 1, &end, 16);
            options->dump_path = end + 1;
        } else if (!strncmp(arg, "--screenshot=", 13)) options->screenshot_path = arg + 13;
        else if (!strncmp(arg, "--exceptions=", 13)) options->exceptions = atoi(arg + 13);
        else if (!strncmp(arg, "--trace=", 8)) headless->trace = fopen(arg + 8, "w");
        else if (!strncmp(arg, "--trace-from=", 13)) options->trace_from = strtoull(arg + 13, NULL, 0);
        else if (!strncmp(arg, "--stop-at=", 10)) options->stop_at = (uint32_t)strtoul(arg + 10, NULL, 16);
        else return false;
    }
    return true;
}

static bool start_machine(headless_t *headless) {
    const options_t *options = &headless->options;
    machine_t *machine = &headless->machine;
    if (!machine_init(machine, options->rom, options->eeprom)) {
        fprintf(stderr, "cannot load %s\n", options->rom);
        return false;
    }
    machine->soc.rcnr = options->rtc_start;
    if (options->load_path) {
        state_result_t loaded = state_load(machine, options->load_path, 0);
        if (loaded != STATE_OK) {
            fprintf(stderr, "%s: %s\n", options->load_path, state_describe(loaded));
            return false;
        }
        headless->start_cycles = machine->cpu.cycles;
    }
    machine_set_optimisations(machine, options->optimisations);
    if (options->backup_reading >= 0) machine->mcu.backup_battery = (uint16_t)options->backup_reading;
    if (options->log) machine->log = stderr;
    machine->cpu.hook_context = headless;
    machine->cpu.on_undefined = undefined_instruction;
    machine->cpu.on_exception = exception_raised;
    if (headless->trace || options->stop_at || options->profiling) machine->cpu.before = before;
    return true;
}

static void print_stats(headless_t *headless, double now) {
    machine_t *machine = &headless->machine;
    sa1110_t *soc = &machine->soc;
    printf("rcnr %08x rtar %08x rtsr %x ", soc->rcnr, soc->rtar, soc->rtsr);
    printf("t %7.1f irqs %6llu instructions %10llu oscr %08x osmr0 %08x oier %x ossr %x icmr %08x%s\n", now,
           (unsigned long long)(headless->exception_counts[6] - headless->last_irqs),
           (unsigned long long)(machine->cpu.instructions - headless->last_instructions), sa1110_oscr(soc, machine->cpu.cycles),
           soc->osmr[0], soc->oier, soc->ossr, soc->icmr, machine->cpu.waiting ? " waiting" : "");
    headless->last_irqs = headless->exception_counts[6];
    headless->last_instructions = machine->cpu.instructions;
}

static void apply_inputs(headless_t *headless, double now) {
    for (int i = 0; i < headless->input_count; i++) {
        input_event_t *event = &headless->inputs[i];
        if (event->at < 0 || event->at > now) continue;
        if (event->kind == INPUT_KEY) machine_key(&headless->machine, (uint8_t)event->code, event->down);
        else machine_touch(&headless->machine, event->down, event->x, event->y);
        event->at = -1;
    }
}

static void record_audio(headless_t *headless) {
    uint32_t rate, count = machine_audio_take(&headless->machine, headless->audio_frames, MACHINE_AUDIO_FRAMES, &rate);
    if (!count) return;
    if (!headless->wav) {
        headless->wav = fopen(headless->options.wav_path, "wb");
        headless->wav_rate = rate;
        uint8_t header[44] = { 0 };
        if (headless->wav) fwrite(header, 1, sizeof header, headless->wav);
    }
    if (headless->wav) fwrite(headless->audio_frames, 4, count, headless->wav);
    headless->wav_frames += count;
}

static void finish_audio(headless_t *headless) {
    uint32_t data = headless->wav_frames * 4;
    uint8_t header[44] = { 'R', 'I', 'F', 'F' };
    uint32_t riff = data + 36, fmt_size = 16, byte_rate = headless->wav_rate * 4;
    uint16_t format = 1, channels = 2, align = 4, bits = 16;
    memcpy(header + 4, &riff, 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    memcpy(header + 16, &fmt_size, 4);
    memcpy(header + 20, &format, 2);
    memcpy(header + 22, &channels, 2);
    memcpy(header + 24, &headless->wav_rate, 4);
    memcpy(header + 28, &byte_rate, 4);
    memcpy(header + 32, &align, 2);
    memcpy(header + 34, &bits, 2);
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &data, 4);
    fseek(headless->wav, 0, SEEK_SET);
    fwrite(header, 1, sizeof header, headless->wav);
    fclose(headless->wav);
    printf("audio: %u frames at %u Hz\n", headless->wav_frames, headless->wav_rate);
}

static void run(headless_t *headless) {
    const options_t *options = &headless->options;
    machine_t *machine = &headless->machine;
    uint64_t total = headless->start_cycles + (uint64_t)(options->seconds * MACHINE_CPU_HZ);
    uint64_t slice = MACHINE_CPU_HZ / 100;
    while (machine->cpu.cycles < total && !machine->powered_off && !headless->stopped) {
        double now = (double)(machine->cpu.cycles - headless->start_cycles) / MACHINE_CPU_HZ;
        if (options->stats_every > 0 && now >= headless->next_stats) {
            print_stats(headless, now);
            headless->next_stats = now + options->stats_every;
        }
        apply_inputs(headless, now);
        uint64_t left = total - machine->cpu.cycles;
        machine_run(machine, left < slice ? left : slice);
        if (options->wav_path) record_audio(headless);
    }
}

static void print_profile(headless_t *headless) {
    profile_entry_t *profile = headless->profile;
    qsort(profile, PROFILE_SLOTS, sizeof profile[0], profile_order);
    uint64_t total_samples = 0;
    for (int i = 0; i < PROFILE_SLOTS; i++) total_samples += profile[i].count;
    for (int i = 0; i < 25 && profile[i].count; i++)
        printf("  %08x %5.1f%%\n", profile[i].pc, 100.0 * (double)profile[i].count / (double)total_samples);
}

static void print_report(headless_t *headless, double host) {
    machine_t *machine = &headless->machine;
    arm_cpu_t *cpu = &machine->cpu;
    printf("emulated %.3fs in %.3fs, %llu instructions (%.1f M/s)%s%s\n", machine_seconds(machine), host,
           (unsigned long long)cpu->instructions, host > 0 ? cpu->instructions / host / 1e6 : 0,
           headless->stopped ? ", stopped" : "", machine->powered_off ? ", powered off" : "");
    machine_print_status(machine, stdout);
    static const char *names[8] = { "reset", "undefined", "swi", "prefetch", "data", "reserved", "irq", "fiq" };
    printf("exceptions:");
    for (int i = 0; i < 8; i++)
        if (headless->exception_counts[i]) printf(" %s %llu", names[i], (unsigned long long)headless->exception_counts[i]);
    printf("\n");
    printf("unknown accesses: %d\n", machine->unknown_count);
    for (int i = 0; i < machine->unknown_count; i++) {
        machine_unknown_t *entry = &machine->unknown[i];
        printf("  %s %08x size %d count %u last %08x pc %08x\n", entry->write ? "write" : "read ", entry->pa, entry->size,
               entry->count, entry->value, entry->pc);
    }
    if (headless->options.profiling) print_profile(headless);
}

static void dump_memory(headless_t *headless) {
    const options_t *options = &headless->options;
    FILE *file = fopen(options->dump_path, "wb");
    for (uint32_t offset = 0; file && offset < options->dump_length; offset += 4) {
        uint32_t word = 0;
        arm_read_virtual(&headless->machine.cpu, options->dump_address + offset, 4, &word);
        fwrite(&word, 4, 1, file);
    }
    if (file) fclose(file);
}

int main(int argc, char **argv) {
    headless_t *headless = calloc(1, sizeof *headless);
    if (!headless) return 1;
    if (!parse_options(headless, argc, argv)) {
        usage();
        return 2;
    }
    if (!start_machine(headless)) return 1;
    const options_t *options = &headless->options;
    clock_t started = clock();
    run(headless);
    print_report(headless, (double)(clock() - started) / CLOCKS_PER_SEC);
    if (headless->wav) finish_audio(headless);
    if (options->save_path && !state_save(&headless->machine, options->save_path, 0)) fprintf(stderr, "cannot save %s\n", options->save_path);
    if (options->dump_path) dump_memory(headless);
    if (options->screenshot_path && !screenshot(&headless->machine, options->screenshot_path))
        fprintf(stderr, "cannot write %s\n", options->screenshot_path);
    if (headless->trace) fclose(headless->trace);
    machine_free(&headless->machine);
    free(headless);
    return 0;
}
