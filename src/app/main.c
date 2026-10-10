#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/machine.h"
#include "core/state.h"

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 240
#define MAX_FRAME_SECONDS 0.1
#define AUTOSAVE_SECONDS 60.0
#define NOTICE_SECONDS 2.0
#define MINIMUM_TOUCH_SECONDS 0.06
#define MINIMUM_GAP_SECONDS   0.03
#define PEN_QUEUE_SIZE        16

typedef struct {
    bool down;
    int x, y;
} pen_event_t;

typedef struct {
    pen_event_t events[PEN_QUEUE_SIZE];
    int head, count;
    bool down;
    double changed_at;
} pen_queue_t;

static void pen_queue_push(pen_queue_t *queue, bool down, int x, int y) {
    if (queue->count) {
        pen_event_t *last = &queue->events[(queue->head + queue->count - 1) % PEN_QUEUE_SIZE];
        if (last->down == down) {
            last->x = x;
            last->y = y;
            return;
        }
    }
    if (queue->count == PEN_QUEUE_SIZE) return;
    queue->events[(queue->head + queue->count++) % PEN_QUEUE_SIZE] = (pen_event_t){ down, x, y };
}

static void pen_queue_run(pen_queue_t *queue, machine_t *machine) {
    double now = machine_seconds(machine);
    while (queue->count) {
        pen_event_t *event = &queue->events[queue->head];
        if (event->down == queue->down) {
            if (event->down) machine_touch(machine, true, event->x, event->y);
        } else {
            double wait = queue->down ? MINIMUM_TOUCH_SECONDS : MINIMUM_GAP_SECONDS;
            if (now - queue->changed_at < wait) return;
            machine_touch(machine, event->down, event->x, event->y);
            queue->down = event->down;
            queue->changed_at = now;
        }
        queue->head = (queue->head + 1) % PEN_QUEUE_SIZE;
        queue->count--;
    }
}

static SDL_Window *window;
static char notice[96];
static double notice_until;

static void show_notice(const char *text, double now) {
    snprintf(notice, sizeof notice, "Jornada 720 - %s", text);
    SDL_SetWindowTitle(window, notice);
    notice_until = now + NOTICE_SECONDS;
}

static void print_status(machine_t *machine) {
    arm_cpu_t *cpu = &machine->cpu;
    sa1110_t *soc = &machine->soc;
    fprintf(stderr, "t %.1fs pc %08x cpsr %08x pid %08x %s instructions %llu\n", machine_seconds(machine), cpu->r[15],
            cpu->cpsr, cpu->pid, cpu->waiting ? "waiting" : "running", (unsigned long long)cpu->instructions);
    fprintf(stderr, "  oscr %08x osmr0 %08x oier %x ossr %x icmr %08x iclr %08x gedr %08x gpio %08x irq %d fiq %d\n",
            sa1110_oscr(soc, cpu->cycles), soc->osmr[0], soc->oier, soc->ossr, soc->icmr, soc->iclr, soc->gedr,
            (soc->gplr_out & soc->gpdr) | (soc->gpio_input & ~soc->gpdr), cpu->irq_line, cpu->fiq_line);
    fprintf(stderr, "  mcu selected %d keys %d pen %d\n", machine->mcu.selected, machine->mcu.key_count, machine->mcu.pen_down);
}

typedef struct {
    SDL_Scancode host;
    uint8_t jornada;
} key_mapping_t;

static const key_mapping_t key_map[] = {
    { SDL_SCANCODE_ESCAPE, 0x01 },
    { SDL_SCANCODE_1, 0x11 }, { SDL_SCANCODE_2, 0x12 }, { SDL_SCANCODE_3, 0x13 }, { SDL_SCANCODE_4, 0x14 },
    { SDL_SCANCODE_5, 0x15 }, { SDL_SCANCODE_6, 0x16 }, { SDL_SCANCODE_7, 0x17 }, { SDL_SCANCODE_8, 0x18 },
    { SDL_SCANCODE_9, 0x19 }, { SDL_SCANCODE_0, 0x1A }, { SDL_SCANCODE_MINUS, 0x1B }, { SDL_SCANCODE_EQUALS, 0x1C },
    { SDL_SCANCODE_Q, 0x21 }, { SDL_SCANCODE_W, 0x22 }, { SDL_SCANCODE_E, 0x23 }, { SDL_SCANCODE_R, 0x24 },
    { SDL_SCANCODE_T, 0x25 }, { SDL_SCANCODE_Y, 0x26 }, { SDL_SCANCODE_U, 0x27 }, { SDL_SCANCODE_I, 0x28 },
    { SDL_SCANCODE_O, 0x29 }, { SDL_SCANCODE_P, 0x2A }, { SDL_SCANCODE_BACKSLASH, 0x2B },
    { SDL_SCANCODE_BACKSPACE, 0x2C },
    { SDL_SCANCODE_A, 0x31 }, { SDL_SCANCODE_S, 0x32 }, { SDL_SCANCODE_D, 0x33 }, { SDL_SCANCODE_F, 0x34 },
    { SDL_SCANCODE_G, 0x35 }, { SDL_SCANCODE_H, 0x36 }, { SDL_SCANCODE_J, 0x37 }, { SDL_SCANCODE_K, 0x38 },
    { SDL_SCANCODE_L, 0x39 }, { SDL_SCANCODE_SEMICOLON, 0x3A },
    { SDL_SCANCODE_Z, 0x41 }, { SDL_SCANCODE_X, 0x42 }, { SDL_SCANCODE_C, 0x43 }, { SDL_SCANCODE_V, 0x44 },
    { SDL_SCANCODE_B, 0x45 }, { SDL_SCANCODE_N, 0x46 }, { SDL_SCANCODE_M, 0x47 }, { SDL_SCANCODE_COMMA, 0x48 },
    { SDL_SCANCODE_PERIOD, 0x49 }, { SDL_SCANCODE_APOSTROPHE, 0x4B }, { SDL_SCANCODE_RETURN, 0x4C }, { SDL_SCANCODE_KP_ENTER, 0x4C },
    { SDL_SCANCODE_TAB, 0x51 }, { SDL_SCANCODE_LSHIFT, 0x53 }, { SDL_SCANCODE_UP, 0x5A }, { SDL_SCANCODE_RSHIFT, 0x5C },
    { SDL_SCANCODE_LALT, 0x65 }, { SDL_SCANCODE_RALT, 0x66 }, { SDL_SCANCODE_LEFT, 0x69 }, { SDL_SCANCODE_DOWN, 0x6A },
    { SDL_SCANCODE_RIGHT, 0x6B }, { SDL_SCANCODE_LGUI, 0x71 }, { SDL_SCANCODE_LCTRL, 0x72 },
    { SDL_SCANCODE_RCTRL, 0x72 }, { SDL_SCANCODE_SPACE, 0x74 }, { SDL_SCANCODE_SLASH, 0x78 },
    { SDL_SCANCODE_DELETE, 0x79 }, { SDL_SCANCODE_F12, 0x7F },
};

static int jornada_key(SDL_Scancode scancode) {
    for (size_t i = 0; i < sizeof key_map / sizeof key_map[0]; i++)
        if (key_map[i].host == scancode) return key_map[i].jornada;
    return -1;
}

int main(int argc, char **argv) {
    const char *rom = "rom/jornada720.bin";
    const char *eeprom = "rom/jorn720_eeprom.bin";
    bool fresh = false;
    bool optimisations = false;
    const char *state_override = NULL;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fresh")) fresh = true;
        else if (!strcmp(argv[i], "--optimisations")) optimisations = true;
        else if (!strncmp(argv[i], "--state=", 8)) state_override = argv[i] + 8;
        else if (positional++ == 0) rom = argv[i];
        else eeprom = argv[i];
    }
    static machine_t machine;
    if (!machine_init(&machine, rom, eeprom)) {
        fprintf(stderr, "cannot load %s or %s\n", rom, eeprom);
        return 1;
    }
    char *preferences = SDL_GetPrefPath(NULL, "jornada-72x-emu");
    char state_path[1024], old_path[1040];
    if (state_override) snprintf(state_path, sizeof state_path, "%s", state_override);
    else snprintf(state_path, sizeof state_path, "%sstate.bin", preferences ? preferences : "");
    snprintf(old_path, sizeof old_path, "%s.old", state_path);
    SDL_free(preferences);
    state_result_t loaded = fresh ? STATE_MISSING : state_load(&machine, state_path, (uint64_t)time(NULL));
    machine_set_optimisations(&machine, optimisations);
    if (loaded == STATE_UNREADABLE || loaded == STATE_INCOMPATIBLE || loaded == STATE_WRONG_ROM) rename(state_path, old_path);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer;
    if (!SDL_CreateWindowAndRenderer("Jornada 720", SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, SDL_WINDOW_RESIZABLE, &window,
                                     &renderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(renderer, 1);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                                             SCREEN_HEIGHT);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    static uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    SDL_AudioSpec audio_spec = { SDL_AUDIO_S16LE, 2, 22050 };
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());
    bool muted = false;
    static int16_t audio_frames[MACHINE_AUDIO_FRAMES * 2];
    bool running = true, button_down = false;
    pen_queue_t pen = { 0 };
    uint64_t last = SDL_GetPerformanceCounter();
    double frequency = (double)SDL_GetPerformanceFrequency();
    double clock_now = 0, next_autosave = AUTOSAVE_SECONDS;
    if (loaded != STATE_MISSING || !fresh) show_notice(state_describe(loaded), 0);
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (event.key.repeat) break;
                if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_GUI) && event.key.scancode == SDL_SCANCODE_Q) {
                    running = false;
                    break;
                }
                if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_GUI)) {
                    switch (event.key.scancode) {
                    case SDL_SCANCODE_R:
                        machine_reset(&machine);
                        show_notice("reset", clock_now);
                        break;
                    case SDL_SCANCODE_S:
                        show_notice(state_save(&machine, state_path, (uint64_t)time(NULL)) ? "state saved" : "cannot save state", clock_now);
                        break;
                    case SDL_SCANCODE_L:
                        show_notice(state_describe(state_load(&machine, state_path, (uint64_t)time(NULL))), clock_now);
                        break;
                    case SDL_SCANCODE_D:
                        print_status(&machine);
                        break;
                    case SDL_SCANCODE_M:
                        muted = !muted;
                        show_notice(muted ? "sound off" : "sound on", clock_now);
                        break;
                    default:
                        break;
                    }
                    if (event.key.scancode == SDL_SCANCODE_R || event.key.scancode == SDL_SCANCODE_S
                        || event.key.scancode == SDL_SCANCODE_L || event.key.scancode == SDL_SCANCODE_D
                        || event.key.scancode == SDL_SCANCODE_M)
                        break;
                }
                int code = jornada_key(event.key.scancode);
                if (code >= 0) machine_key(&machine, (uint8_t)code, event.type == SDL_EVENT_KEY_DOWN);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION: {
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    if (button_down) pen_queue_push(&pen, true, (int)event.motion.x, (int)event.motion.y);
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    button_down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    pen_queue_push(&pen, button_down, (int)event.button.x, (int)event.button.y);
                }
                break;
            }
            default:
                break;
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - last) / frequency;
        last = now;
        clock_now += elapsed;
        if (elapsed > MAX_FRAME_SECONDS) elapsed = MAX_FRAME_SECONDS;
        if (notice_until && clock_now > notice_until) {
            notice_until = 0;
            SDL_SetWindowTitle(window, "Jornada 720");
        }
        if (clock_now >= next_autosave) {
            next_autosave = clock_now + AUTOSAVE_SECONDS;
            state_save(&machine, state_path, (uint64_t)time(NULL));
        }
        pen_queue_run(&pen, &machine);
        machine_run(&machine, (uint64_t)(elapsed * MACHINE_CPU_HZ));
        pen_queue_run(&pen, &machine);
        uint32_t audio_rate;
        uint32_t audio_count = machine_audio_take(&machine, audio_frames, MACHINE_AUDIO_FRAMES, &audio_rate);
        if (audio && audio_count && !muted && audio_rate) {
            if ((int)audio_rate != audio_spec.freq) {
                audio_spec.freq = (int)audio_rate;
                SDL_SetAudioStreamFormat(audio, &audio_spec, NULL);
            }
            SDL_PutAudioStreamData(audio, audio_frames, (int)(audio_count * 4));
        }
        sed1356_render(&machine.display, pixels, SCREEN_WIDTH, SCREEN_HEIGHT);
        SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * (int)sizeof pixels[0]);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    if (!state_save(&machine, state_path, (uint64_t)time(NULL))) fprintf(stderr, "cannot save %s\n", state_path);
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    machine_free(&machine);
    return 0;
}
