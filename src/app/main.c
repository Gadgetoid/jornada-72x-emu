#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/desktop.h"
#include "app/input.h"
#include "app/launch.h"
#include "app/notices.h"
#include "app/paths.h"
#include "app/serial_service.h"
#include "app/view.h"
#include "core/agent.h"
#include "core/machine.h"
#include "core/state.h"
#include "rapi/rapi.h"

#define WINDOW_TITLE "Jornada 720"
#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 240
#define MAX_FRAME_SECONDS 0.1
#define AUTOSAVE_SECONDS 60.0

typedef struct {
    launch_t launch;
    machine_t machine;
    agent_t *agent;
    serial_service_t serial;
    desktop_t *desktop;
    char state_path[1024];
    state_result_t loaded;
    SDL_Window *window;
    SDL_Renderer *renderer;
    view_t *view;
    SDL_AudioStream *audio;
    SDL_AudioSpec audio_spec;
    int16_t audio_frames[MACHINE_AUDIO_FRAMES * 2];
    bool muted;
    bool running;
    bool button_down;
    pen_queue_t pen;
    notice_t notice;
    uint64_t last_frame;
    double frequency;
    double clock_now;
    double next_autosave;
} app_t;

static bool handle_command(app_t *app, SDL_Scancode scancode) {
    machine_t *machine = &app->machine;
    switch (scancode) {
    case SDL_SCANCODE_Q:
        app->running = false;
        return true;
    case SDL_SCANCODE_R:
        machine_soft_reset(machine);
        notice_show(&app->notice, "soft reset", NOTICE_SHORT);
        return true;
    case SDL_SCANCODE_S:
        notice_show(&app->notice, state_save(machine, app->state_path, (uint64_t)time(NULL)) ? "state saved" : "cannot save state", NOTICE_SHORT);
        return true;
    case SDL_SCANCODE_L:
        notice_show(&app->notice, state_describe(state_load(machine, app->state_path, (uint64_t)time(NULL))), NOTICE_SHORT);
        return true;
    case SDL_SCANCODE_D:
        machine_print_status(machine, stderr);
        return true;
    case SDL_SCANCODE_M:
        app->muted = !app->muted;
        notice_show(&app->notice, app->muted ? "sound off" : "sound on", NOTICE_SHORT);
        return true;
    default:
        return false;
    }
}

static void handle_key(app_t *app, const SDL_KeyboardEvent *key) {
    if (key->repeat) return;
    if (key->down && (key->mod & SDL_KMOD_GUI) && handle_command(app, key->scancode)) return;
    uint8_t scancode;
    if (input_find_scancode(key->scancode, &scancode)) machine_key(&app->machine, scancode, key->down);
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

static void update_title(app_t *app) {
    char title[NOTICE_TEXT_CAPACITY + 32];
    const char *notice = notice_current(&app->notice);
    if (notice) snprintf(title, sizeof title, "%s - %s", WINDOW_TITLE, notice);
    else snprintf(title, sizeof title, "%s", WINDOW_TITLE);
    if (strcmp(SDL_GetWindowTitle(app->window), title)) SDL_SetWindowTitle(app->window, title);
}

static void step_timers(app_t *app) {
    update_title(app);
    if (app->clock_now >= app->next_autosave) {
        app->next_autosave = app->clock_now + AUTOSAVE_SECONDS;
        state_save(&app->machine, app->state_path, (uint64_t)time(NULL));
    }
}

static void step_machine(app_t *app, double elapsed) {
    if (app->agent) agent_poll(app->agent, &app->machine.mailbox);
    pen_queue_run(&app->pen, &app->machine);
    machine_run(&app->machine, (uint64_t)(elapsed * MACHINE_CPU_HZ));
    pen_queue_run(&app->pen, &app->machine);
    serial_service_step(&app->serial, &app->machine, &app->notice);
}

static void step_desktop(app_t *app) {
    char message[NOTICE_TEXT_CAPACITY];
    if (serial_service_take_desktop(&app->serial) && app->agent && !machine_agent_running(&app->machine))
        desktop_install_debugmgr(app->desktop);
    if (desktop_take_status(app->desktop, message, sizeof message)) notice_show(&app->notice, message, NOTICE_MEDIUM);
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

static void run_frame(app_t *app) {
    poll_host_events(app);
    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - app->last_frame) / app->frequency;
    app->last_frame = now;
    app->clock_now += elapsed;
    if (elapsed > MAX_FRAME_SECONDS) elapsed = MAX_FRAME_SECONDS;
    step_timers(app);
    step_machine(app, elapsed);
    step_desktop(app);
    step_audio(app);
    view_render(app->view, &app->machine.display);
}

static void log_stderr(const char *message) {
    fputs(message, stderr);
}

static bool start_machine(app_t *app) {
    launch_t *launch = &app->launch;
    if (!machine_init(&app->machine, launch->rom, launch->eeprom)) {
        fprintf(stderr, "jornada720: cannot load %s or %s\n", launch->rom, launch->eeprom);
        return false;
    }
    if (launch->state_file) snprintf(app->state_path, sizeof app->state_path, "%s", launch->state_file);
    else app_state_path(app->state_path, sizeof app->state_path);
    app->loaded = launch->fresh ? STATE_MISSING : state_load(&app->machine, app->state_path, (uint64_t)time(NULL));
    machine_set_optimisations(&app->machine, launch->optimisations);
    if (app->loaded == STATE_UNREADABLE || app->loaded == STATE_INCOMPATIBLE || app->loaded == STATE_WRONG_ROM) {
        char old_path[sizeof app->state_path + 8];
        snprintf(old_path, sizeof old_path, "%s.old", app->state_path);
        rename(app->state_path, old_path);
    }
    if (launch->agent_socket && !(app->agent = agent_create(launch->agent_socket, log_stderr))) {
        fprintf(stderr, "jornada720: cannot listen on agent socket %s\n", launch->agent_socket);
        return false;
    }
    serial_service_init(&app->serial, launch->tcp_port ? SERIAL_TCP : launch->network ? SERIAL_NETWORK : SERIAL_OFF, launch->tcp_port, log_stderr);
    const char *failure = serial_service_start(&app->serial, &app->machine);
    if (failure) {
        fprintf(stderr, "jornada720: %s\n", failure);
        return false;
    }
    char sync_manifest[1024];
    rapi_data_path("sync-manifest.txt", sync_manifest, sizeof sync_manifest);
    app->desktop = desktop_create(app->serial.rapi_socket, sync_manifest);
    return true;
}

static bool open_window(app_t *app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "jornada720: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, SDL_WINDOW_RESIZABLE, &app->window,
                                     &app->renderer)) {
        fprintf(stderr, "jornada720: SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return false;
    }
    app->view = view_create(app->renderer);
    if (!app->view) {
        fprintf(stderr, "jornada720: cannot create the screen texture: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static void open_audio(app_t *app) {
    app->audio_spec = (SDL_AudioSpec){ SDL_AUDIO_S16LE, 2, 22050 };
    app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &app->audio_spec, NULL, NULL);
    if (app->audio) SDL_ResumeAudioStreamDevice(app->audio);
    else fprintf(stderr, "jornada720: no audio: %s\n", SDL_GetError());
}

static void shut_down(app_t *app) {
    if (!state_save(&app->machine, app->state_path, (uint64_t)time(NULL))) fprintf(stderr, "jornada720: cannot save %s\n", app->state_path);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    view_destroy(app->view);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    SDL_Quit();
    desktop_destroy(app->desktop);
    serial_service_close(&app->serial);
    agent_destroy(app->agent);
    machine_free(&app->machine);
}

int main(int argc, char **argv) {
    app_t *app = calloc(1, sizeof *app);
    if (!app) return 1;
    launch_parse(&app->launch, argc, argv);
    if (!start_machine(app)) return 1;
    if (!open_window(app)) return 1;
    open_audio(app);
    app->running = true;
    app->last_frame = SDL_GetPerformanceCounter();
    app->frequency = (double)SDL_GetPerformanceFrequency();
    app->next_autosave = AUTOSAVE_SECONDS;
    if (app->loaded != STATE_MISSING || !app->launch.fresh) notice_show(&app->notice, state_describe(app->loaded), NOTICE_SHORT);
    while (app->running) run_frame(app);
    shut_down(app);
    free(app);
    return 0;
}
