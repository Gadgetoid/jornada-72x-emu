#include "app/input.h"

#define MINIMUM_TOUCH_SECONDS 0.06
#define MINIMUM_GAP_SECONDS   0.03

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

bool input_find_scancode(SDL_Scancode host, uint8_t *scancode) {
    for (size_t i = 0; i < sizeof key_map / sizeof key_map[0]; i++) {
        if (key_map[i].host == host) {
            *scancode = key_map[i].jornada;
            return true;
        }
    }
    return false;
}

void pen_queue_push(pen_queue_t *queue, bool down, int x, int y) {
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

void pen_queue_run(pen_queue_t *queue, machine_t *machine) {
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
