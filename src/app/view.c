#include "app/view.h"

#include <stdlib.h>

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 240

struct view {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
};

view_t *view_create(SDL_Renderer *renderer) {
    view_t *view = calloc(1, sizeof *view);
    if (!view) return NULL;
    view->renderer = renderer;
    view->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                                      SCREEN_HEIGHT);
    if (!view->texture) {
        free(view);
        return NULL;
    }
    SDL_SetTextureScaleMode(view->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(renderer, 1);
    return view;
}

void view_destroy(view_t *view) {
    if (!view) return;
    SDL_DestroyTexture(view->texture);
    free(view);
}

void view_render(view_t *view, const sed1356_t *display) {
    sed1356_render(display, view->pixels, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_UpdateTexture(view->texture, NULL, view->pixels, SCREEN_WIDTH * (int)sizeof view->pixels[0]);
    SDL_RenderClear(view->renderer);
    SDL_RenderTexture(view->renderer, view->texture, NULL, NULL);
    SDL_RenderPresent(view->renderer);
}
