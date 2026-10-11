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

typedef struct {
    const char *rom;
    const char *eeprom;
    const char *state_override;
    bool fresh;
    bool optimisations;
    machine_t machine;
    char state_path[1024];
    char old_path[1040];
    state_result_t loaded;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    SDL_AudioStream *audio;
    SDL_AudioSpec audio_spec;
    int16_t audio_frames[MACHINE_AUDIO_FRAMES * 2];
    bool muted;
    bool running;
    bool button_down;
    pen_queue_t pen;
    uint64_t last_frame;
    double frequency;
    double clock_now;
    double next_autosave;
    char notice[96];
    double notice_until;
} app_t;

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

static void show_notice(app_t *app, const char *text) {
    snprintf(app->notice, sizeof app->notice, "Jornada 720 - %s", text);
    SDL_SetWindowTitle(app->window, app->notice);
    app->notice_until = app->clock_now + NOTICE_SECONDS;
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

static bool handle_command(app_t *app, SDL_Scancode scancode) {
    machine_t *machine = &app->machine;
    switch (scancode) {
    case SDL_SCANCODE_Q:
        app->running = false;
        return true;
    case SDL_SCANCODE_R:
        machine_reset(machine);
        show_notice(app, "reset");
        return true;
    case SDL_SCANCODE_S:
        show_notice(app, state_save(machine, app->state_path, (uint64_t)time(NULL)) ? "state saved" : "cannot save state");
        return true;
    case SDL_SCANCODE_L:
        show_notice(app, state_describe(state_load(machine, app->state_path, (uint64_t)time(NULL))));
        return true;
    case SDL_SCANCODE_D:
        print_status(machine);
        return true;
    case SDL_SCANCODE_M:
        app->muted = !app->muted;
        show_notice(app, app->muted ? "sound off" : "sound on");
        return true;
    default:
        return false;
    }
}

static void handle_key(app_t *app, const SDL_KeyboardEvent *key) {
    if (key->repeat) return;
    if (key->down && (key->mod & SDL_KMOD_GUI) && handle_command(app, key->scancode)) return;
    int code = jornada_key(key->scancode);
    if (code >= 0) machine_key(&app->machine, (uint8_t)code, key->down);
}

static void handle_mouse(app_t *app, SDL_Event *event) {
    SDL_ConvertEventToRenderCoordinates(app->renderer, event);
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (app->button_down) pen_queue_push(&app->pen, true, (int)event->motion.x, (int)event->motion.y);
    } else if (event->button.button == SDL_BUTTON_LEFT) {
        app->button_down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        pen_queue_push(&app->pen, app->button_down, (int)event->button.x, (int)event->button.y);
    }
}

static void poll_host_events(app_t *app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            app->running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            handle_key(app, &event.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse(app, &event);
            break;
        default:
            break;
        }
    }
}

static void step_timers(app_t *app) {
    if (app->notice_until && app->clock_now > app->notice_until) {
        app->notice_until = 0;
        SDL_SetWindowTitle(app->window, "Jornada 720");
    }
    if (app->clock_now >= app->next_autosave) {
        app->next_autosave = app->clock_now + AUTOSAVE_SECONDS;
        state_save(&app->machine, app->state_path, (uint64_t)time(NULL));
    }
}

static void step_machine(app_t *app, double elapsed) {
    pen_queue_run(&app->pen, &app->machine);
    machine_run(&app->machine, (uint64_t)(elapsed * MACHINE_CPU_HZ));
    pen_queue_run(&app->pen, &app->machine);
}

static void step_audio(app_t *app) {
    uint32_t audio_rate;
    uint32_t audio_count = machine_audio_take(&app->machine, app->audio_frames, MACHINE_AUDIO_FRAMES, &audio_rate);
    if (!app->audio || !audio_count || app->muted || !audio_rate) return;
    if ((int)audio_rate != app->audio_spec.freq) {
        app->audio_spec.freq = (int)audio_rate;
        SDL_SetAudioStreamFormat(app->audio, &app->audio_spec, NULL);
    }
    SDL_PutAudioStreamData(app->audio, app->audio_frames, (int)(audio_count * 4));
}

static void sync_screen(app_t *app) {
    sed1356_render(&app->machine.display, app->pixels, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_UpdateTexture(app->texture, NULL, app->pixels, SCREEN_WIDTH * (int)sizeof app->pixels[0]);
    SDL_RenderClear(app->renderer);
    SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
    SDL_RenderPresent(app->renderer);
}

static void run_frame(app_t *app) {
    poll_host_events(app);
    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - app->last_frame) / app->frequency;
    app->last_frame = now;
    app->clock_now += elapsed;
    if (elapsed > MAX_FRAME_SECONDS) elapsed = MAX_FRAME_SECONDS;
    step_timers(app);
    step_machine(app, elapsed);
    step_audio(app);
    sync_screen(app);
}

static void load_launch(app_t *app, int argc, char **argv) {
    app->rom = "rom/jornada720.bin";
    app->eeprom = "rom/jorn720_eeprom.bin";
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fresh")) app->fresh = true;
        else if (!strcmp(argv[i], "--optimisations")) app->optimisations = true;
        else if (!strncmp(argv[i], "--state=", 8)) app->state_override = argv[i] + 8;
        else if (positional++ == 0) app->rom = argv[i];
        else app->eeprom = argv[i];
    }
}

static bool start_machine(app_t *app) {
    if (!machine_init(&app->machine, app->rom, app->eeprom)) {
        fprintf(stderr, "cannot load %s or %s\n", app->rom, app->eeprom);
        return false;
    }
    char *preferences = SDL_GetPrefPath(NULL, "jornada-72x-emu");
    if (app->state_override) snprintf(app->state_path, sizeof app->state_path, "%s", app->state_override);
    else snprintf(app->state_path, sizeof app->state_path, "%sstate.bin", preferences ? preferences : "");
    snprintf(app->old_path, sizeof app->old_path, "%s.old", app->state_path);
    SDL_free(preferences);
    app->loaded = app->fresh ? STATE_MISSING : state_load(&app->machine, app->state_path, (uint64_t)time(NULL));
    machine_set_optimisations(&app->machine, app->optimisations);
    if (app->loaded == STATE_UNREADABLE || app->loaded == STATE_INCOMPATIBLE || app->loaded == STATE_WRONG_ROM)
        rename(app->state_path, app->old_path);
    return true;
}

static bool open_window(app_t *app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer("Jornada 720", SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, SDL_WINDOW_RESIZABLE, &app->window,
                                     &app->renderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(app->renderer, 1);
    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                                     SCREEN_HEIGHT);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);
    return true;
}

static void open_audio(app_t *app) {
    app->audio_spec = (SDL_AudioSpec){ SDL_AUDIO_S16LE, 2, 22050 };
    app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &app->audio_spec, NULL, NULL);
    if (app->audio) SDL_ResumeAudioStreamDevice(app->audio);
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());
}

static void shut_down(app_t *app) {
    if (!state_save(&app->machine, app->state_path, (uint64_t)time(NULL))) fprintf(stderr, "cannot save %s\n", app->state_path);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    SDL_DestroyTexture(app->texture);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    SDL_Quit();
    machine_free(&app->machine);
}

int main(int argc, char **argv) {
    app_t *app = calloc(1, sizeof *app);
    if (!app) return 1;
    load_launch(app, argc, argv);
    if (!start_machine(app)) return 1;
    if (!open_window(app)) return 1;
    open_audio(app);
    app->running = true;
    app->last_frame = SDL_GetPerformanceCounter();
    app->frequency = (double)SDL_GetPerformanceFrequency();
    app->next_autosave = AUTOSAVE_SECONDS;
    if (app->loaded != STATE_MISSING || !app->fresh) show_notice(app, state_describe(app->loaded));
    while (app->running) run_frame(app);
    shut_down(app);
    free(app);
    return 0;
}
