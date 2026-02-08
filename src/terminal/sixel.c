#include "sixel.h"
#include "terminal.h"
#include <sixel.h>
#include <stdlib.h>
#include <string.h>

static int sixel_write_cb(char *data, int size, void *priv) {
    (void)priv;
    safe_write(data, (size_t)size);
    return size;
}

static sixel_output_t *sixel_out = NULL;
static sixel_dither_t *sixel_dith = NULL;
static uint8_t *sixel_pixels = NULL;
static size_t sixel_pixels_cap = 0;

void render_sixel(const uint8_t *buffer, uint32_t width, uint32_t height) {
    safe_write("\x1b[H", 3);

    size_t data_size = (size_t)width * height * 4;

    if (!sixel_out) {
        if (sixel_output_new(&sixel_out, sixel_write_cb, NULL, NULL) != SIXEL_OK)
            return;
    }

    if (sixel_pixels_cap < data_size) {
        free(sixel_pixels);
        sixel_pixels = (uint8_t *)malloc(data_size);
        sixel_pixels_cap = data_size;
    }
    memcpy(sixel_pixels, buffer, data_size);

    if (sixel_dith)
        sixel_dither_unref(sixel_dith);
    if (sixel_dither_new(&sixel_dith, 256, NULL) != SIXEL_OK)
        return;

    sixel_dither_initialize(sixel_dith, sixel_pixels, width, height,
                            SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_NORM,
                            SIXEL_REP_CENTER_BOX, SIXEL_QUALITY_LOW);

    sixel_encode(sixel_pixels, width, height, 4, sixel_dith, sixel_out);
}
