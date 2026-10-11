#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/capture.h"
#include "app/host.h"
#include "app/log.h"
#include "frontend/android/android.h"
#include "frontend/common/dialog.h"
#include "frontend/common/menu.h"
#include "net/net_gateway.h"
#include "util/file.h"

static bool confirm_reset(SDL_Window *window, const char *name) {
    char title[160];
    snprintf(title, sizeof title, "Reset %s?", name);
    return host_confirm(window, title,
                        "A reset is a cold boot back to the factory state: it clears RAM, including files, settings and installed programs. A backup of the machine goes in Snapshots/Backups first. Soft Reset keeps them.",
                        "Reset");
}

static bool handle_machine_menu(app_t *app, int item, int *switch_to, bool *events_seen) {
    if (item == MENU_NEW_MACHINE) {
        dialog_rom_t *rom_list = calloc(32, sizeof *rom_list);
        if (!rom_list) return true;
        int rom_count = library_list_roms(rom_list, 32);
        dialog_machine_t chosen = { .memory = DIALOG_MEMORY_SIZES[0], .screen = SCREEN_PRESETS[0], .host_time = app->settings.host_time != 0 };
        if (rom_count) snprintf(chosen.rom, sizeof chosen.rom, "%s", app->current.rom);
        *events_seen = true;
        bool created = dialog_new_machine(app->window, rom_list, rom_count, rom_catalog_label, &chosen);
        free(rom_list);
        if (!created) return true;
        profile_t made = { .host_time = chosen.host_time };
        snprintf(made.rom, sizeof made.rom, "%s", chosen.rom);
        library_find_roms(&app->roms);
        snprintf(made.eeprom, sizeof made.eeprom, "%s", app->roms.eeprom);
        if (chosen.name[0]) snprintf(made.name, sizeof made.name, "%s", chosen.name);
        else profile_default_name(&made, made.name, sizeof made.name);
        profile_make_unique(&app->profiles, &made, app->profiles_folder);
        if (!profile_save(&made, app->profiles_folder)) {
            notice_show(&app->notice, "could not save the new machine", NOTICE_MEDIUM);
            return true;
        }
        profiles_load(&app->profiles, app->profiles_folder);
        if (app->current.id[0]) app->current_index = profile_find(&app->profiles, app->current.id);
        *switch_to = profile_find(&app->profiles, made.id);
        return true;
    }
    if (item != MENU_MANAGE_MACHINES) return false;

    const char *names[PROFILES_MAX];
    for (int i = 0; i < app->profiles.count; i++) names[i] = app->profiles.entries[i].name;
    int chosen = app->current_index >= 0 ? app->current_index : 0;
    *events_seen = true;
    dialog_manage_t action = app->profiles.count ? dialog_manage_machines(app->window, names, app->profiles.count, app->current_index, &chosen) : DIALOG_MANAGE_CLOSE;
    if (action == DIALOG_MANAGE_CLOSE || chosen < 0 || chosen >= app->profiles.count) return true;
    profile_t picked_profile = app->profiles.entries[chosen];
    char message[300];
    if (action == DIALOG_MANAGE_RESET) {
        if (!confirm_reset(app->window, picked_profile.name)) return true;
        if (chosen == app->current_index) {
            snapshot_store_backup_machine(&app->snapshots, app->session.machine, app->session.state_path);
            app->since_backup = 0;
            machine_reset(app->session.machine);
        } else {
            snapshot_store_backup_file(&app->snapshots, picked_profile.state);
            remove(picked_profile.state);
        }
        snprintf(message, sizeof message, "reset %s; the machine before it is in Snapshots/Backups", picked_profile.name);
    } else {
        if (chosen == app->current_index) {
            notice_show(&app->notice, "switch to another machine before deleting this one", NOTICE_LONG);
            return true;
        }
        char title[160];
        snprintf(title, sizeof title, "Delete %s?", picked_profile.name);
        if (!host_confirm(app->window, title, "This removes the machine and its saved state. A backup of the state goes in Snapshots/Backups first.", "Delete")) return true;
        snapshot_store_backup_file(&app->snapshots, picked_profile.state);
        profile_delete(&picked_profile, app->profiles_folder);
        char current_id[sizeof app->current.id];
        snprintf(current_id, sizeof current_id, "%s", app->current.id);
        profiles_load(&app->profiles, app->profiles_folder);
        app->current_index = current_id[0] ? profile_find(&app->profiles, current_id) : -1;
        snprintf(message, sizeof message, "deleted %s", picked_profile.name);
    }
    notice_show(&app->notice, message, NOTICE_LONG);
    return true;
}

static bool handle_view_menu(app_t *app, int item) {
    settings_t *settings = &app->settings;
    switch (item) {
    case MENU_SCALE_50:
    case MENU_SCALE_75:
    case MENU_SCALE_100:
    case MENU_SCALE_150:
    case MENU_SCALE_200:
    case MENU_ZOOM_IN:
    case MENU_ZOOM_OUT: {
        int index = settings_scale_index(settings->scale);
        if (item == MENU_ZOOM_IN) index = index + 1 < SETTINGS_SCALE_COUNT ? index + 1 : index;
        else if (item == MENU_ZOOM_OUT) index = index > 0 ? index - 1 : index;
        else index = item - MENU_SCALE_50;
        settings->scale = settings_scale_at(index);
        settings_save(settings);
        view_fit_window(app->view, settings->scale);
        return true;
    }
    case MENU_FULL_SCREEN:
        SDL_SetWindowFullscreen(app->window, !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN));
        return true;
    case MENU_COPY_SCREEN:
        notice_show(&app->notice, capture_copy_screen(app->view) ? "screen copied" : "could not copy the screen", NOTICE_SHORT);
        return true;
    case MENU_SAVE_SCREENSHOT: {
        char path[1100];
        char message[1200];
        if (capture_save_screenshot(app->view, path, sizeof path)) snprintf(message, sizeof message, "saved %s", file_leaf_name(path));
        else snprintf(message, sizeof message, "could not save the screenshot");
        notice_show(&app->notice, message, NOTICE_MEDIUM);
        return true;
    }
    }
    return false;
}

void app_command(app_t *app, int item, int *switch_to, bool *events_seen) {
    if (handle_machine_menu(app, item, switch_to, events_seen)) return;
    if (handle_view_menu(app, item)) return;
    machine_t *machine = app->session.machine;
    settings_t *settings = &app->settings;
    switch (item) {
    case MENU_POWER:
        machine_power_button(machine, true);
        app->power_release_at = machine_cycles(machine) + (uint64_t)(POWER_PRESS_SECONDS * MACHINE_CLOCK_HZ);
        break;
    case MENU_PAUSE: app->paused = !app->paused; break;
    case MENU_SOUND: app->sound = !app->sound; break;
    case MENU_SOFT_RESET:
        machine_soft_reset(machine);
        serial_service_soft_reset(&app->serial, machine);
        break;
    case MENU_PASTE: {
        char *clipboard = SDL_GetClipboardText();
        size_t typed = clipboard ? typer_start(&app->typer, app->key_layout, clipboard) : 0;
        SDL_free(clipboard);
        char message[64];
        snprintf(message, sizeof message, typed ? "typing %zu characters" : "nothing to type", typed);
        notice_show(&app->notice, message, NOTICE_SHORT);
        break;
    }
    case MENU_SAVE_STATE:
        notice_show(&app->notice, machine_save(machine, app->session.state_path, (int64_t)time(NULL)) ? "state saved" : "could not save state", NOTICE_SHORT);
        break;
    case MENU_LOAD_STATE:
        snapshot_store_backup_machine(&app->snapshots, machine, app->session.state_path);
        notice_show(&app->notice, machine_load(machine, app->session.state_path, (int64_t)time(NULL)) ? "state loaded" : "no saved state", NOTICE_SHORT);
        serial_service_reattach(&app->serial, machine);
        break;
    case MENU_SHOW_STATE: host_reveal_file(app->session.state_path); break;
    case MENU_SAVE_SNAPSHOT: {
        static const SDL_DialogFileFilter filters[] = { { "Snapshot", "state" } };
        char default_snapshot[1200];
        snapshot_store_default_name(&app->snapshots, default_snapshot, sizeof default_snapshot);
        SDL_ShowSaveFileDialog(picks_done, (void *)(intptr_t)PICK_SAVE_SNAPSHOT, app->window, filters, 1, default_snapshot);
        break;
    }
    case MENU_LOAD_SNAPSHOT: {
        static const SDL_DialogFileFilter filters[] = { { "Snapshot", "state;bin" } };
        char folder[1100];
        snapshot_store_folder(&app->snapshots, folder, sizeof folder);
        SDL_ShowOpenFileDialog(picks_done, (void *)(intptr_t)PICK_LOAD_SNAPSHOT, app->window, filters, 1, folder, false);
        break;
    }
    case MENU_QUIT:
        app->running = false;
        break;
    case MENU_GDB_SERVER:
        if (app->debugger) {
            app_runner_set_debugger_locked(app->runner, NULL);
            gdb_destroy(app->debugger);
            app->debugger = NULL;
            settings->gdb_server = 0;
            notice_show(&app->notice, "GDB server stopped", NOTICE_LONG);
        } else {
            char message[160];
            app->debugger = app_start_network_gdb(machine, settings->gdb_port, message, sizeof message);
            app_runner_set_debugger_locked(app->runner, app->debugger);
            settings->gdb_server = app->debugger != NULL;
            notice_show(&app->notice, message, NOTICE_LONG);
            app->debugmgr_wanted = app->debugger != NULL;
        }
        settings_save(settings);
        break;
#ifdef __ANDROID__
    case MENU_IMPORT: {
        int cards, imported = library_import_files(&cards);
        library_find_roms(&app->roms);
        char message[160];
        snprintf(message, sizeof message, "imported %d ROMs and %d cards", imported, cards);
        notice_show(&app->notice, message, NOTICE_MEDIUM);
        break;
    }
    case MENU_FETCH_DOCUMENTS:
    case MENU_SHARED_FOLDER: {
        if (!android_all_files_access()) {
            android_request_all_files_access();
            notice_show(&app->notice, "allow All files access for Jornada 720, then try again", NOTICE_LONG);
            break;
        }
        bool shared = item == MENU_SHARED_FOLDER;
        picked_t *folder = calloc(1, sizeof *folder);
        const char *start = shared && settings->shared_folder[0] ? settings->shared_folder : "/storage/emulated/0/Documents";
        if (folder && android_choose_folder(shared ? "Folder to share with My Documents" : "Folder to copy My Documents into", start, folder->paths[0], sizeof folder->paths[0])) {
            folder->kind = shared ? PICK_SHARED : PICK_FETCH;
            folder->count = 1;
            free(app->picked);
            app->picked = folder;
        } else {
            free(folder);
        }
        *events_seen = true;
        break;
    }
#else
    case MENU_FETCH_DOCUMENTS:
        SDL_ShowOpenFolderDialog(picks_done, (void *)(intptr_t)PICK_FETCH, app->window, NULL, false);
        break;
    case MENU_SHARED_FOLDER:
        SDL_ShowOpenFolderDialog(picks_done, (void *)(intptr_t)PICK_SHARED, app->window, settings->shared_folder[0] ? settings->shared_folder : NULL, false);
        break;
#endif
    case MENU_SPEED_1:
    case MENU_SPEED_2:
        settings->speed = item == MENU_SPEED_1 ? 1 : 2;
        machine_set_speed(machine, settings->speed);
        settings_save(settings);
        break;
    case MENU_OPTIMISATIONS:
        settings->optimisations = !settings->optimisations;
        machine_set_optimisations(machine, settings->optimisations != 0);
        settings_save(settings);
        break;
    default:
        if (item >= MENU_SERIAL_PORT_FIRST && item <= MENU_SERIAL_PORT_LAST && item - MENU_SERIAL_PORT_FIRST < app->serial.port_count) {
            notice_show(&app->notice, serial_service_select(&app->serial, machine, SERIAL_DEVICE, app->serial.ports[item - MENU_SERIAL_PORT_FIRST]), NOTICE_LONG);
        }
        if (item >= MENU_MACHINE_FIRST && item <= MENU_MACHINE_LAST && item - MENU_MACHINE_FIRST < app->profiles.count && item - MENU_MACHINE_FIRST != app->current_index) {
            *switch_to = item - MENU_MACHINE_FIRST;
        }
        break;
    case MENU_SERIAL_OFF:
    case MENU_SERIAL_NETWORK:
    case MENU_SERIAL_PTY:
    case MENU_SERIAL_TCP: {
        serial_mode_t mode = item == MENU_SERIAL_NETWORK ? SERIAL_NETWORK : item == MENU_SERIAL_PTY ? SERIAL_PTY : item == MENU_SERIAL_TCP ? SERIAL_TCP : SERIAL_OFF;
        notice_show(&app->notice, serial_service_select(&app->serial, machine, mode, NULL), NOTICE_LONG);
        break;
    }
    case MENU_INSERT_CARD:
    case MENU_INSERT_PC_CARD: {
        static const SDL_DialogFileFilter filters[] = { { "Card images", "img;bin;raw" }, { "All files", "*" } };
        char folder[1100];
        library_folder("cards", folder, sizeof folder);
        SDL_ShowOpenFileDialog(picks_done, (void *)(intptr_t)(item == MENU_INSERT_CARD ? PICK_CARD : PICK_PC_CARD), app->window, filters, 2, folder, false);
        break;
    }
    case MENU_EJECT_CARD:
    case MENU_EJECT_PC_CARD:
        machine_eject_card(machine, item == MENU_EJECT_CARD ? MACHINE_SOCKET_CF : MACHINE_SOCKET_PC_CARD);
        notice_show(&app->notice, "card ejected", NOTICE_SHORT);
        break;
    case MENU_SEND_FILES:
        SDL_ShowOpenFileDialog(picks_done, (void *)(intptr_t)PICK_SEND, app->window, NULL, 0, NULL, true);
        break;
    case MENU_SYNC_NOW:
        desktop_sync(app->desktop, settings->shared_folder);
        break;
    case MENU_SET_PROXY:
        desktop_set_proxy(app->desktop);
        break;
    case MENU_BAUD_19200:
    case MENU_BAUD_38400:
    case MENU_BAUD_57600:
    case MENU_BAUD_115200:
        desktop_set_baud(app->desktop, item == MENU_BAUD_19200 ? 19200 : item == MENU_BAUD_38400 ? 38400 : item == MENU_BAUD_57600 ? 57600 : 115200);
        break;
    case MENU_NETWORK_RAPI: {
        settings->network_rapi = !settings->network_rapi;
        settings_save(settings);
        char address[64];
        host_local_address(address, sizeof address);
        char message[160];
        if (settings->network_rapi) snprintf(message, sizeof message, "RAPI at %s:%u", address, settings->rapi_port);
        else snprintf(message, sizeof message, "RAPI over the network off");
        serial_service_update_rapi(&app->serial, machine);
        notice_show(&app->notice, message, NOTICE_LONG);
        break;
    }
    case MENU_STOP_SHARING: {
        char message[1200];
        snprintf(message, sizeof message, "stopped sharing %s", file_leaf_name(settings->shared_folder));
        settings->shared_folder[0] = 0;
        settings_save(settings);
        notice_show(&app->notice, message, NOTICE_SHORT);
        break;
    }
    }
}

void app_switch_machine(app_t *app, int index) {
    char switch_notice[NOTICE_TEXT_CAPACITY];
    machine_session_t next_session = { 0 };
    profile_t next_profile = app->profiles.entries[index];
    if (!app_start_session(app, &next_session, &next_profile, NULL, false, switch_notice, sizeof switch_notice)) {
        notice_show(&app->notice, switch_notice, NOTICE_MEDIUM);
        return;
    }
    machine_t *machine = app->session.machine;
    if (app->pen_down) machine_touch(machine, false, 0, 0);
    input_clear(&app->input);
    app->pen_down = false;
    app->typer.length = app->typer.position = 0;
    machine_save(machine, app->session.state_path, (int64_t)time(NULL));
    machine_session_destroy(&app->session);
    app->session = next_session;
    machine = app->session.machine;
    app->current = next_profile;
    app->current_index = index;
    app->key_layout = machine_key_layout(machine);
    snprintf(app->settings.machine, sizeof app->settings.machine, "%s", app->current.id);
    settings_save(&app->settings);
    serial_service_attach_machine(&app->serial, machine);
    app->power_release_at = 0;
    app->since_backup = 0;
    app_runner_set_machine_locked(app->runner, machine);
    if (app->debugger) gdb_set_machine(app->debugger, machine);
    char message[160];
    snprintf(message, sizeof message, "switched to %s", app->current.name);
    notice_show(&app->notice, switch_notice[0] ? switch_notice : message, NOTICE_MEDIUM);
}

void app_handle_picked(app_t *app) {
    machine_t *machine = app->session.machine;
    settings_t *settings = &app->settings;
    picked_t *picked = app->picked;
    if (picked->kind == PICK_CARD || picked->kind == PICK_PC_CARD) {
        int socket = picked->kind == PICK_CARD ? MACHINE_SOCKET_CF : MACHINE_SOCKET_PC_CARD;
        notice_show(&app->notice, machine_insert_card(machine, socket, picked->paths[0]) ? "card inserted" : "could not open card image", NOTICE_SHORT);
    } else if (picked->kind == PICK_SAVE_SNAPSHOT) {
        char path[1100];
        snprintf(path, sizeof path, "%s%s", picked->paths[0], file_has_extension(picked->paths[0], ".state") ? "" : ".state");
        bool saved = machine_save(machine, path, (int64_t)time(NULL));
#ifdef __ANDROID__
        if (saved && picked->export_uri[0]) {
            saved = android_export(path, picked->export_uri);
            remove(path);
        }
#endif
        char message[1200];
        snprintf(message, sizeof message, saved ? "saved snapshot %s" : "could not save %s", file_leaf_name(path));
        notice_show(&app->notice, message, NOTICE_MEDIUM);
    } else if (picked->kind == PICK_SEND) {
        const char *files[PICK_MAX + 1];
        for (int i = 0; i < picked->count; i++) files[i] = picked->paths[i];
        files[picked->count] = NULL;
        desktop_send(app->desktop, files);
    } else if (picked->kind == PICK_FETCH) {
        desktop_fetch(app->desktop, picked->paths[0]);
    } else if (picked->kind == PICK_SHARED) {
        snprintf(settings->shared_folder, sizeof settings->shared_folder, "%s", picked->paths[0]);
        settings_save(settings);
        char message[1200];
        snprintf(message, sizeof message, "sharing %s with \\My Documents", file_leaf_name(settings->shared_folder));
        notice_show(&app->notice, message, NOTICE_MEDIUM);
        if (serial_service_online(&app->serial)) desktop_sync(app->desktop, settings->shared_folder);
    } else if (picked->kind == PICK_LOAD_SNAPSHOT) {
        char message[1200];
        if (machine_state_matches(machine, picked->paths[0])) snapshot_store_backup_machine(&app->snapshots, machine, app->session.state_path);
        if (machine_load(machine, picked->paths[0], (int64_t)time(NULL))) {
            serial_service_reattach(&app->serial, machine);
            snprintf(message, sizeof message, "loaded snapshot %s", file_leaf_name(picked->paths[0]));
        } else {
            snprintf(message, sizeof message, "%s isn't a snapshot of this ROM", file_leaf_name(picked->paths[0]));
        }
        notice_show(&app->notice, message, NOTICE_MEDIUM);
    }
    free(picked);
    app->picked = NULL;
}

void app_update_menus(app_t *app, bool device_online) {
    machine_t *machine = app->session.machine;
    const settings_t *settings = &app->settings;
    const serial_service_t *serial = &app->serial;
    bool desktop_free = device_online && !desktop_busy(app->desktop);
    menu_ensure();
    menu_set_enabled(MENU_SEND_FILES, desktop_free);
    menu_set_enabled(MENU_FETCH_DOCUMENTS, desktop_free);
    menu_set_enabled(MENU_SYNC_NOW, desktop_free && settings->shared_folder[0]);
    menu_set_enabled(MENU_STOP_SHARING, settings->shared_folder[0] != 0);
    menu_set_enabled(MENU_SET_PROXY, desktop_free);
    static const uint32_t LINK_SPEEDS[] = { 19200, 38400, 57600, 115200 };
    bool speed_settable = desktop_free;
    uint32_t link_baud = device_online ? machine_serial_baud(machine) : 0;
    for (int baud_item = MENU_BAUD_19200; baud_item <= MENU_BAUD_115200; baud_item++) {
        uint32_t speed = LINK_SPEEDS[baud_item - MENU_BAUD_19200];
        menu_set_enabled(baud_item, speed_settable);
        menu_set_checked(baud_item, link_baud && link_baud * 20 > speed * 19 && link_baud * 20 < speed * 21);
    }
    menu_set_checked(MENU_NETWORK_RAPI, settings->network_rapi != 0);
    menu_set_enabled(MENU_EJECT_CARD, machine_card_inserted(machine, MACHINE_SOCKET_CF));
    menu_set_enabled(MENU_EJECT_PC_CARD, machine_card_inserted(machine, MACHINE_SOCKET_PC_CARD));
    menu_set_enabled(MENU_SERIAL_NETWORK, net_gateway_available());
    menu_set_checked(MENU_SERIAL_OFF, serial->link.mode == SERIAL_OFF);
    menu_set_checked(MENU_SERIAL_NETWORK, serial->link.mode == SERIAL_NETWORK);
    menu_set_checked(MENU_SERIAL_PTY, serial->link.mode == SERIAL_PTY);
    menu_set_checked(MENU_SERIAL_TCP, serial->link.mode == SERIAL_TCP);
    for (int i = 0; i < SERIAL_PORT_MAX; i++) {
        int port_item = MENU_SERIAL_PORT_FIRST + i;
        bool shown = i < serial->port_count || (i == 0 && serial->port_count == 0);
        menu_set_hidden(port_item, !shown);
        if (!shown) continue;
        menu_set_title(port_item, serial->port_count ? serial->ports[i] + 5 : "No serial ports found");
        menu_set_enabled(port_item, serial->port_count > 0);
        menu_set_checked(port_item, serial->port_count && serial->link.mode == SERIAL_DEVICE && !strcmp(serial->link.name, serial->ports[i]));
    }
    menu_set_checked(MENU_PAUSE, app->paused);
    menu_set_checked(MENU_GDB_SERVER, app->debugger != NULL);
    menu_set_checked(MENU_SOUND, app->sound);
    for (int i = 0; i < PROFILES_MAX; i++) {
        int machine_item = MENU_MACHINE_FIRST + i;
        menu_set_hidden(machine_item, i >= app->profiles.count);
        if (i >= app->profiles.count) continue;
        menu_set_title(machine_item, app->profiles.entries[i].name);
        menu_set_checked(machine_item, i == app->current_index);
    }
    menu_set_enabled(MENU_NEW_MACHINE, app->profiles.count < PROFILES_MAX);
    for (int scale_item = MENU_SCALE_50; scale_item <= MENU_SCALE_200; scale_item++) menu_set_checked(scale_item, settings->scale == settings_scale_at(scale_item - MENU_SCALE_50));
    menu_set_enabled(MENU_ZOOM_IN, settings->scale < settings_scale_at(SETTINGS_SCALE_COUNT - 1));
    menu_set_enabled(MENU_ZOOM_OUT, settings->scale > settings_scale_at(0));
    menu_set_checked(MENU_FULL_SCREEN, (SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN) != 0);
    menu_set_checked(MENU_SPEED_1, machine_speed(machine) == 1);
    menu_set_checked(MENU_SPEED_2, machine_speed(machine) == 2);
    menu_set_checked(MENU_OPTIMISATIONS, machine_optimisations(machine));
}

