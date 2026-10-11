#include "app/view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/machine.h"

#define WINDOW_SCALE  2
#define SCREEN_WIDTH  MACHINE_SCREEN_WIDTH
#define SCREEN_HEIGHT MACHINE_SCREEN_HEIGHT
#define SCREEN_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)

struct view {
    SDL_Window    *window;
    SDL_Renderer  *renderer;
    SDL_Texture   *texture;
    int output_w, output_h;
    float left, top, right, bottom;
    int bar_height;
    bool laid_out;
    SDL_FRect dest;
    uint32_t frame[SCREEN_PIXELS];
    uint32_t shown[SCREEN_PIXELS];
};

view_t *view_create(SDL_Window *window, SDL_Renderer *renderer, int top) {
    view_t *view = calloc(1, sizeof *view);
    if (!view) return NULL;
    view->window = window;
    view->renderer = renderer;
    view->top = (float)top;
    view->bar_height = top;
    view->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!view->texture) {
        free(view);
        return NULL;
    }
    SDL_UpdateTexture(view->texture, NULL, view->shown, SCREEN_WIDTH * (int)sizeof view->shown[0]);
    return view;
}

void view_window_size(uint32_t scale, int bar_height, int *width, int *height) {
    *width = SCREEN_WIDTH * WINDOW_SCALE * (int)scale / 100;
    *height = SCREEN_HEIGHT * WINDOW_SCALE * (int)scale / 100 + bar_height;
}

void view_fit_window(view_t *view, uint32_t scale) {
#ifdef __ANDROID__
    (void)view;
    (void)scale;
    return;
#endif
    if (SDL_GetWindowFlags(view->window) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(view->window, false);
    int width, height;
    view_window_size(scale, view->bar_height, &width, &height);
    SDL_SetWindowSize(view->window, width, height);
}

void view_destroy(view_t *view) {
    if (!view) return;
    if (view->texture) SDL_DestroyTexture(view->texture);
    free(view);
}

void view_set_insets(view_t *view, int left, int top, int right, int bottom) {
    if (view->left == (float)left && view->top == (float)top && view->right == (float)right && view->bottom == (float)bottom) return;
    view->left = (float)left;
    view->top = (float)top;
    view->right = (float)right;
    view->bottom = (float)bottom;
    view->laid_out = false;
}

static bool layout(view_t *view) {
    int output_w, output_h;
    SDL_GetRenderOutputSize(view->renderer, &output_w, &output_h);
    if (view->laid_out && output_w == view->output_w && output_h == view->output_h) return false;
    view->output_w = output_w;
    view->output_h = output_h;
    view->laid_out = true;

    int window_w, window_h;
    SDL_GetWindowSize(view->window, &window_w, &window_h);
    float ratio = window_h > 0 ? (float)output_h / window_h : 0;
    float left = floorf(view->left * ratio), top = floorf(view->top * ratio);
    float area_w = output_w - left - floorf(view->right * ratio);
    float area_h = output_h - top - floorf(view->bottom * ratio);
    float fit = fminf(area_w / SCREEN_WIDTH, area_h / SCREEN_HEIGHT);
    bool whole = fabsf(fit - roundf(fit)) < 0.01f && fit >= 1.0f;
    float scale = whole ? roundf(fit) : fit;
    SDL_SetTextureScaleMode(view->texture, whole ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    view->dest.w = SCREEN_WIDTH * scale;
    view->dest.h = SCREEN_HEIGHT * scale;
    view->dest.x = left + floorf((area_w - view->dest.w) / 2);
#ifdef __ANDROID__
    if (output_h > output_w) view->dest.y = top;
    else
#endif
    view->dest.y = top + floorf((area_h - view->dest.h) / 2);
    return true;
}

uint32_t *view_frame(view_t *view) {
    return view->frame;
}

bool view_update(view_t *view) {
    bool changed = layout(view);
    if (memcmp(view->frame, view->shown, sizeof view->shown)) {
        memcpy(view->shown, view->frame, sizeof view->shown);
        SDL_UpdateTexture(view->texture, NULL, view->shown, SCREEN_WIDTH * (int)sizeof view->shown[0]);
        changed = true;
    }
    return changed;
}

void view_render(view_t *view) {
    SDL_SetRenderDrawColor(view->renderer, 0, 0, 0, 255);
    SDL_RenderClear(view->renderer);
    SDL_RenderTexture(view->renderer, view->texture, NULL, &view->dest);
}

bool view_screen_position(view_t *view, float window_x, float window_y, int *x, int *y) {
    int window_w, window_h;
    SDL_GetWindowSize(view->window, &window_w, &window_h);
    if (!window_w || !window_h || view->dest.w <= 0 || view->dest.h <= 0) return false;
    float pixel_x = window_x * view->output_w / window_w, pixel_y = window_y * view->output_h / window_h;
    float screen_x = (pixel_x - view->dest.x) / view->dest.w * SCREEN_WIDTH;
    float screen_y = (pixel_y - view->dest.y) / view->dest.h * SCREEN_HEIGHT;
    *x = (int)screen_x;
    *y = (int)screen_y;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= SCREEN_WIDTH) *x = SCREEN_WIDTH - 1;
    if (*y >= SCREEN_HEIGHT) *y = SCREEN_HEIGHT - 1;
    return screen_x >= 0 && screen_y >= 0 && screen_x < SCREEN_WIDTH && screen_y < SCREEN_HEIGHT;
}

const uint32_t *view_image(view_t *view, int *width, int *height) {
    *width = SCREEN_WIDTH;
    *height = SCREEN_HEIGHT;
    return view->shown;
}
