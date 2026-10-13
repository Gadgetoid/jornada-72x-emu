#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/app.h"
#include "app/desktop.h"
#include "app/host.h"
#include "app/input.h"
#include "app/launch.h"
#include "app/library.h"
#include "app/log.h"
#include "app/machine_session.h"
#include "app/notices.h"
#include "app/paths.h"
#include "app/picks.h"
#include "app/profiles.h"
#include "app/runner.h"
#include "app/serial_service.h"
#include "app/settings.h"
#include "app/snapshot_store.h"
#include "app/typer.h"
#include "app/view.h"
#include "core/agent.h"
#include "core/gdb.h"
#include "core/machine.h"
#include "frontend/android/android.h"
#include "frontend/common/menu.h"
#include "net/net_gateway.h"
#include "net/ppp_gateway.h"
#include "rapi/rapi.h"
#include "util/file.h"
#include "util/options.h"

#define IDLE_FRAME_NS    (SDL_NS_PER_SECOND / 60)
#define MAX_FRAME_SLICE  0.1
#define AUTOSAVE_SECONDS 60
#define BACKUP_SECONDS   600
#define WINDOW_TITLE     "Jornada 720"

static void release_keys(input_queue_t *input, machine_t *machine, bool *held, int only_modifiers_up) {
    static const struct { uint8_t scancode; int modifier; } modifiers[] = {
        { 0x53, MENU_MOD_SHIFT }, { 0x5C, MENU_MOD_SHIFT }, { 0x72, MENU_MOD_CONTROL }, { 0x65, MENU_MOD_ALT }, { 0x66, MENU_MOD_ALT },
    };
    if (only_modifiers_up < 0) {
        for (int i = 0; i < 256; i++) {
            if (!held[i]) continue;
            held[i] = false;
            input_add(input, machine, INPUT_KEY, false, 0, 0, (uint8_t)i);
        }
        return;
    }
    if (!(only_modifiers_up & MENU_MOD_KNOWN)) return;
    for (size_t i = 0; i < sizeof modifiers / sizeof modifiers[0]; i++) {
        uint8_t scancode = modifiers[i].scancode;
        if (held[scancode] && !(only_modifiers_up & modifiers[i].modifier)) {
            held[scancode] = false;
            input_add(input, machine, INPUT_KEY, false, 0, 0, scancode);
        }
    }
}

gdb_t *app_start_network_gdb(machine_t *machine, uint32_t port, char *notice, size_t size) {
    gdb_t *gdb = gdb_create(machine, (int)port, true, app_log_always);
    char address[64];
    host_local_address(address, sizeof address);
    if (gdb) snprintf(notice, size, "GDB server at %s:%u", address, port);
    else snprintf(notice, size, "cannot listen for GDB on port %u", port);
    return gdb;
}

static void set_title(SDL_Window *window, const char *name, const char *notice, bool paused, bool suspended) {
    char base[200], title[1400];
    snprintf(base, sizeof base, "%s (%s)", WINDOW_TITLE, name);
    if (notice) snprintf(title, sizeof title, "%s: %s", base, notice);
    else if (paused) snprintf(title, sizeof title, "%s, paused", base);
    else if (suspended) snprintf(title, sizeof title, "%s, suspended", base);
    else snprintf(title, sizeof title, "%s", base);
    if (strcmp(SDL_GetWindowTitle(window), title)) SDL_SetWindowTitle(window, title);
}

bool app_start_session(app_t *app, machine_session_t *session, const profile_t *profile, const char *state_file, bool fresh,
                       char *notice, size_t notice_size) {
    if (!machine_session_start(session, profile, app->settings.speed, state_file, fresh, &app->snapshots, notice, notice_size)) return false;
    machine_set_optimisations(session->machine, app->settings.optimisations != 0);
    return true;
}

static bool poll_host_events(app_t *app) {
    machine_t *machine = app->session.machine;
    bool events_seen = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        events_seen = true;
        if (menu_event(&event)) {
            if (menu_active()) {
                release_keys(&app->input, machine, app->held, -1);
                if (app->pen_down) input_add(&app->input, machine, INPUT_PEN, false, 0, 0, 0);
                app->pen_down = false;
            }
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_TERMINATING:
            app->running = false;
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            machine_save(machine, app->session.state_path, (int64_t)time(NULL));
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool down = event.type == SDL_EVENT_KEY_DOWN;
            uint8_t scancode;
            if (!input_find_scancode(app->key_layout, event.key.key, &scancode)) break;
            if (down) {
                if (event.key.repeat || (event.key.mod & SDL_KMOD_GUI) || app->held[scancode]) break;
                app->held[scancode] = true;
                input_add(&app->input, machine, INPUT_KEY, true, 0, 0, scancode);
            } else if (app->held[scancode]) {
                app->held[scancode] = false;
                input_add(&app->input, machine, INPUT_KEY, false, 0, 0, scancode);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            scroller_add(&app->scroller, event.wheel.y, event.wheel.x);
            break;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data && app->dropped.count < PICK_MAX) snprintf(app->dropped.paths[app->dropped.count++], sizeof app->dropped.paths[0], "%s", event.drop.data);
            break;
        case SDL_EVENT_DROP_COMPLETE:
            if (app->dropped.count) {
                char message[NOTICE_TEXT_CAPACITY];
                picks_handle_drop(&app->dropped, machine, app->desktop, serial_service_online(&app->serial) && !desktop_busy(app->desktop), message, sizeof message);
                notice_show(&app->notice, message, NOTICE_MEDIUM);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            library_find_roms(&app->roms);
#ifdef __ANDROID__
            SDL_SetWindowFullscreen(app->window, false);
            SDL_SetWindowFullscreen(app->window, true);
#endif
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            release_keys(&app->input, machine, app->held, -1);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                if (view_screen_position(app->view, event.button.x, event.button.y, &x, &y)) {
                    app->pen_down = true;
                    input_add(&app->input, machine, INPUT_PEN, true, x, y, 0);
                }
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (app->pen_down) {
                int x, y;
                view_screen_position(app->view, event.motion.x, event.motion.y, &x, &y);
                pen_move(&app->input, machine, x, y);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT && app->pen_down) {
                int x, y;
                view_screen_position(app->view, event.button.x, event.button.y, &x, &y);
                app->pen_down = false;
                input_add(&app->input, machine, INPUT_PEN, false, x, y, 0);
            }
            break;
        default: {
            picked_t *picked = picks_take(&event);
            if (picked) {
                free(app->picked);
                app->picked = picked;
            }
            break;
        }
        }
    }
    return events_seen;
}

static void poll_desktop(app_t *app, bool device_online) {
    machine_t *machine = app->session.machine;
    serial_link_t *link = &app->serial.link;
    bool debugging = app->debugger || app->agent;
    if (link->gateway && net_gateway_take_desktop_connected(ppp_gateway_network(link->gateway))) {
        if (app->settings.shared_folder[0]) desktop_sync(app->desktop, app->settings.shared_folder);
        app->debugmgr_wanted = debugging;
    }
    if (!device_online) app->debugmgr_wanted = false;
    if (app->debugmgr_wanted && !desktop_busy(app->desktop)) {
        app->debugmgr_wanted = false;
        if (debugging && !machine_agent_running(machine)) desktop_install_debugmgr(app->desktop);
    }
    if (desktop_take_reconnect(app->desktop)) serial_service_replug(&app->serial, machine, SPEED_SETTLE_SECONDS);
    char message[256];
    if (desktop_take_status(app->desktop, message, sizeof message)) notice_show(&app->notice, message, NOTICE_MEDIUM);
    bool ready = machine_agent_running(machine);
    if (ready != app->debugmgr_ready) fprintf(stderr, ready ? "debugmgr: ready\n" : "debugmgr: stopped\n");
    app->debugmgr_ready = ready;
}

static void step_buttons(app_t *app) {
    machine_t *machine = app->session.machine;
    if (app->power_release_at && machine_cycles(machine) >= app->power_release_at) {
        app->power_release_at = 0;
        machine_power_button(machine, false);
    }
}

static void step_timers(app_t *app, double elapsed) {
    machine_t *machine = app->session.machine;
    app->since_autosave += elapsed;
    if (!app->paused) app->since_backup += elapsed;
    if (app->since_backup >= BACKUP_SECONDS) {
        app->since_backup = 0;
        snapshot_store_backup_machine(&app->snapshots, machine, app->session.state_path);
    }
    if (app->since_autosave >= AUTOSAVE_SECONDS) {
        app->since_autosave = 0;
        machine_save(machine, app->session.state_path, (int64_t)time(NULL));
    }
}

static void step_audio(app_t *app) {
    uint32_t rate;
    for (size_t count; (count = machine_audio(app->session.machine, app->samples, AUDIO_CHUNK, &rate)) > 0;) {
        if (!app->audio || !app->sound || !rate) continue;
        if ((int)rate != app->audio_spec.freq) {
            app->audio_spec.freq = (int)rate;
            SDL_SetAudioStreamFormat(app->audio, &app->audio_spec, NULL);
        }
        SDL_PutAudioStreamData(app->audio, app->samples, (int)(count * sizeof app->samples[0]));
    }
}

static void sync_screen(app_t *app) {
    machine_t *machine = app->session.machine;
#ifdef __ANDROID__
    android_update(false, !machine_suspended(machine));
#endif
    step_audio(app);
    machine_screen(machine, view_frame(app->view));
}

static void run_frame(app_t *app) {
    uint64_t frame_start = SDL_GetTicksNS();
    app_runner_lock(app->runner);
    bool events_seen = poll_host_events(app);

    release_keys(&app->input, app->session.machine, app->held, menu_modifiers());
    int switch_to = -1;
    for (int item = menu_poll(); item >= 0; item = menu_poll()) {
        release_keys(&app->input, app->session.machine, app->held, -1);
        app_command(app, item, &switch_to, &events_seen);
    }
    if (switch_to >= 0 && switch_to < app->profiles.count && switch_to != app->current_index) app_switch_machine(app, switch_to);
    if (app->picked) app_handle_picked(app);
    bool device_online = serial_service_online(&app->serial);
    poll_desktop(app, device_online);
    serial_service_step(&app->serial, app->session.machine, &app->notice);
    app_update_menus(app, device_online);
    step_buttons(app);
    host_reap_children();

    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - app->last_frame) / app->frequency;
    app->last_frame = now;
    if (elapsed > MAX_FRAME_SLICE) elapsed = MAX_FRAME_SLICE;
    const char *notice = notice_current(&app->notice);
    set_title(app->window, app->current.name, notice, app->paused, machine_suspended(app->session.machine));
#ifdef __ANDROID__
    if (android_toast(notice ? notice : app->paused ? "Paused" : NULL)) events_seen = true;
#endif
    step_timers(app, elapsed);
    app_runner_set_paused_locked(app->runner, app->paused);
    typer_step(&app->typer, app->session.machine);
    scroller_step(&app->scroller, app->session.machine);
    sync_screen(app);
    app_runner_unlock(app->runner);

    int inset_left, inset_top, inset_right, inset_bottom;
    menu_insets(&inset_left, &inset_top, &inset_right, &inset_bottom);
    view_set_insets(app->view, inset_left, inset_top, inset_right, inset_bottom);
    bool screen_changed = view_update(app->view);
    if (screen_changed || events_seen || menu_active()) {
        view_render(app->view);
        menu_draw(app->renderer);
        SDL_RenderPresent(app->renderer);
    } else {
        uint64_t spent = SDL_GetTicksNS() - frame_start;
        if (spent < IDLE_FRAME_NS) SDL_DelayNS(IDLE_FRAME_NS - spent);
    }
}

static int load_launch(app_t *app, int argc, char **argv, bool *start) {
    *start = false;
#ifdef __ANDROID__
    const char *storage = SDL_GetAndroidExternalStoragePath();
    if (storage) {
        setenv("XDG_DATA_HOME", storage, 1);
        setenv("XDG_CONFIG_HOME", storage, 1);
    }
    const char *cache = SDL_GetAndroidCachePath();
    if (cache) setenv("TMPDIR", cache, 1);
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
#endif
    app->settings = settings_load();
    options_result_t parsed = launch_parse(&app->launch, &app->settings, argc, argv);
    if (parsed == OPTIONS_EXIT) return 0;
    if (parsed == OPTIONS_ERROR) return 2;
    char base[1024];
    app_data_folder(base, sizeof base);
    snapshot_store_init(&app->snapshots, base);
    app_log_set_verbose(app->launch.verbose);
    *start = true;
    return 0;
}

static int start_machine(app_t *app) {
    settings_t *settings = &app->settings;
    const launch_t *launch = &app->launch;
    library_find_roms(&app->roms);
    library_machines_folder(app->profiles_folder, sizeof app->profiles_folder);
    profiles_load(&app->profiles, app->profiles_folder);
    if (!app->profiles.count) machine_session_default_profile(&app->profiles, &app->roms, settings, app->profiles_folder);
    app->current_index = -1;
    if (launch->rom) {
        app->current = (profile_t){ .host_time = settings->host_time != 0 };
        snprintf(app->current.rom, sizeof app->current.rom, "%s", launch->rom);
        snprintf(app->current.eeprom, sizeof app->current.eeprom, "%s", launch->eeprom ? launch->eeprom : app->roms.eeprom);
        snprintf(app->current.name, sizeof app->current.name, "%s", file_leaf_name(launch->rom));
    } else {
        if (launch->machine) {
            app->current_index = profile_find(&app->profiles, launch->machine);
            if (app->current_index < 0) {
                fprintf(stderr, "jornada720: no machine called %s\n", launch->machine);
                return 2;
            }
        } else {
            app->current_index = settings->machine[0] ? profile_find(&app->profiles, settings->machine) : -1;
            if (app->current_index < 0) app->current_index = app->profiles.count ? 0 : -1;
        }
#ifdef __ANDROID__
        while (app->current_index < 0 && library_first_run_import()) {
            library_find_roms(&app->roms);
            machine_session_default_profile(&app->profiles, &app->roms, settings, app->profiles_folder);
            app->current_index = app->profiles.count ? 0 : -1;
        }
#endif
        if (app->current_index < 0) {
            library_show_no_roms();
            return 1;
        }
        app->current = app->profiles.entries[app->current_index];
    }
    char notice[NOTICE_TEXT_CAPACITY];
    if (!app_start_session(app, &app->session, &app->current, launch->state_file, launch->fresh, notice, sizeof notice)) {
        fprintf(stderr, "jornada720: %s\n", notice);
        return 1;
    }
    if (app->current_index >= 0) {
        snprintf(settings->machine, sizeof settings->machine, "%s", app->current.id);
        settings_save(settings);
    }
    notice_show(&app->notice, notice, NOTICE_LONG);
    return 0;
}

static bool open_window(app_t *app) {
    app->key_layout = machine_key_layout(app->session.machine);
    SDL_SetAppMetadata(WINDOW_TITLE, options_version(), "jornada-72x-emu");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "jornada720: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) fprintf(stderr, "jornada720: audio: %s\n", SDL_GetError());
    picks_init();
    int window_width, window_height;
    view_window_size(app->settings.scale, menu_bar_height(), &window_width, &window_height);
    app->window = SDL_CreateWindow(WINDOW_TITLE, window_width, window_height, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    app->renderer = app->window ? SDL_CreateRenderer(app->window, NULL) : NULL;
    if (!app->renderer) {
        fprintf(stderr, "jornada720: SDL: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(app->renderer, 1);
#ifdef __ANDROID__
    SDL_SetWindowFullscreen(app->window, true);
#endif
    app->view = view_create(app->window, app->renderer, menu_bar_height());
    if (!app->view) {
        fprintf(stderr, "jornada720: cannot create the screen texture: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static void open_audio(app_t *app) {
    app->audio_spec = (SDL_AudioSpec){ SDL_AUDIO_S16, MACHINE_AUDIO_CHANNELS, 22050 };
    app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &app->audio_spec, NULL, NULL);
    if (app->audio) SDL_ResumeAudioStreamDevice(app->audio);
    else fprintf(stderr, "jornada720: audio: %s\n", SDL_GetError());
    app->sound = true;
}

static int start_debugging(app_t *app) {
    const launch_t *launch = &app->launch;
    if (launch->gdb_process && !launch->gdb_port) {
        fprintf(stderr, "jornada720: --gdb-process needs --gdb\n");
        return 2;
    }
    if (launch->gdb_port) {
        app->debugger = gdb_create(app->session.machine, launch->gdb_port, false, app_log_always);
        if (!app->debugger) {
            fprintf(stderr, "jornada720: cannot listen for GDB on port %d\n", launch->gdb_port);
            return 1;
        }
        if (launch->gdb_process) gdb_set_process(app->debugger, launch->gdb_process);
    }
    if (!app->debugger && app->settings.gdb_server) {
        char message[160];
        app->debugger = app_start_network_gdb(app->session.machine, app->settings.gdb_port, message, sizeof message);
        if (!notice_current(&app->notice)) notice_show(&app->notice, message, NOTICE_LONG);
    }
    return 0;
}

static void open_serial(app_t *app) {
    char sync_manifest[1024];
    serial_service_init(&app->serial, &app->settings);
    app->serial.ir.tcp_port = app->launch.ir_tcp_port;
    rapi_data_path("sync-manifest.txt", sync_manifest, sizeof sync_manifest);
    app->desktop = desktop_create(app->serial.rapi_socket, sync_manifest);
    const char *failure = serial_service_start(&app->serial, app->session.machine);
    if (failure) fprintf(stderr, "jornada720: %s\n", failure);
}

static void shut_down(app_t *app) {
    machine_t *machine = app->session.machine;
    app_runner_destroy(app->runner);
    gdb_destroy(app->debugger);
    app->debugger = NULL;
    agent_destroy(app->agent);
    app->agent = NULL;
    desktop_destroy(app->desktop);
    serial_service_close(&app->serial);
    free(app->picked);
    if (!machine_save(machine, app->session.state_path, (int64_t)time(NULL))) fprintf(stderr, "jornada720: cannot save %s\n", app->session.state_path);
    if (app_log_verbose()) machine_print_status(machine, stderr);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    view_destroy(app->view);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    SDL_Quit();
    machine_session_destroy(&app->session);
}

int main(int argc, char **argv) {
    app_t *app = calloc(1, sizeof *app);
    if (!app) return 1;
    bool start;
    int result = load_launch(app, argc, argv, &start);
    if (result || !start) return result;
    result = start_machine(app);
    if (result) return result;
    if (!open_window(app)) return 1;
    machine_t *machine = app->session.machine;
    if (app->launch.card && !machine_insert_card(machine, MACHINE_SOCKET_CF, app->launch.card)) fprintf(stderr, "jornada720: cannot open card image %s\n", app->launch.card);
    if (app->launch.pc_card && !machine_insert_card(machine, MACHINE_SOCKET_PC_CARD, app->launch.pc_card)) fprintf(stderr, "jornada720: cannot open card image %s\n", app->launch.pc_card);
    menu_install(app->window);
    open_audio(app);
    app->running = true;
    app->last_frame = SDL_GetPerformanceCounter();
    app->frequency = (double)SDL_GetPerformanceFrequency();
    result = start_debugging(app);
    if (result) return result;
    open_serial(app);
    if (app->launch.agent_socket && !(app->agent = agent_create(app->launch.agent_socket, app_log_always))) {
        fprintf(stderr, "jornada720: cannot listen on agent socket %s\n", app->launch.agent_socket);
        return 1;
    }
    app->runner = app_runner_create(machine, &app->input, app->debugger, app->agent, &app->serial.link, &app->serial.ir);
    if (!app->runner) {
        fprintf(stderr, "jornada720: cannot start machine runner\n");
        return 1;
    }
    while (app->running) run_frame(app);
    shut_down(app);
    free(app);
    return 0;
}
