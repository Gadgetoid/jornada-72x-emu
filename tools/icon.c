#include <stdio.h>
#include <stdlib.h>

#include "util/png.h"
#include "vendor/nanosvg.h"
#include "vendor/nanosvgrast.h"

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: icon SVG SIZE PNG\n");
        return 2;
    }
    int size = atoi(argv[2]);
    if (size <= 0 || size > 4096) {
        fprintf(stderr, "icon: bad size %s\n", argv[2]);
        return 2;
    }
    NSVGimage *image = nsvgParseFromFile(argv[1], "px", 96);
    if (!image || image->width <= 0) {
        fprintf(stderr, "icon: cannot read %s\n", argv[1]);
        return 1;
    }
    unsigned char *rgba = malloc((size_t)size * size * 4);
    uint32_t *pixels = malloc((size_t)size * size * 4);
    NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
    if (!rgba || !pixels || !rasterizer) return 1;
    nsvgRasterize(rasterizer, image, 0, 0, size / image->width, rgba, size, size, size * 4);
    for (size_t i = 0; i < (size_t)size * size; i++) {
        const unsigned char *pixel = rgba + i * 4;
        pixels[i] = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 | (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
    }
    uint8_t *png;
    size_t length;
    if (!png_encode_rgba(pixels, size, size, &png, &length)) return 1;
    FILE *file = fopen(argv[3], "wb");
    if (!file || fwrite(png, 1, length, file) != length || fclose(file) != 0) {
        fprintf(stderr, "icon: cannot write %s\n", argv[3]);
        return 1;
    }
    return 0;
}
