#include "sixel.h"
#include "core/platform_compat.h"
#include "terminal.h"
#include <sixel.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static char *sixel_buffer = NULL;
static size_t sixel_buffer_size = 0;
static size_t sixel_buffer_cap = 0;

static int sixel_write_cb(char *data, int size, void *priv) {
    (void)priv;
    if (sixel_buffer_size + size > sixel_buffer_cap) {
        size_t new_cap = sixel_buffer_cap == 0 ? (1024 * 1024) : sixel_buffer_cap * 2;
        while (sixel_buffer_size + size > new_cap) {
            new_cap *= 2;
        }
        char *new_buf = (char *)realloc(sixel_buffer, new_cap);
        if (!new_buf) return -1;
        sixel_buffer = new_buf;
        sixel_buffer_cap = new_cap;
    }
    memcpy(sixel_buffer + sixel_buffer_size, data, size);
    sixel_buffer_size += size;
    return size;
}

static sixel_output_t *sixel_out = NULL;
static sixel_dither_t *sixel_dith = NULL;
static uint8_t *sixel_pixels = NULL;
static size_t sixel_pixels_cap = 0;
static bool sixel_initialized = false;

static void sixel_cleanup(void) {
    /* Re-enable sixel scrolling */
    safe_write("\x1b[?80l", 6);
}

void render_sixel(const uint8_t *buffer, uint32_t width, uint32_t height) {
    if (!sixel_initialized) {
        /* Disable sixel scrolling */
        safe_write("\x1b[?80h", 6);
        atexit(sixel_cleanup);
        sixel_initialized = true;
    }

    safe_write("\x1b[H", 3);

    const size_t data_size = (size_t)width * height * 4;

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

    sixel_dither_initialize(sixel_dith, sixel_pixels, width, height, SIXEL_PIXELFORMAT_RGBA8888,
                            SIXEL_LARGE_NORM, SIXEL_REP_CENTER_BOX, SIXEL_QUALITY_LOW);

    sixel_buffer_size = 0;
    sixel_encode(sixel_pixels, width, height, 4, sixel_dith, sixel_out);
    if (sixel_buffer_size > 0) {
        safe_write(sixel_buffer, sixel_buffer_size);
    }
}

bool detect_sixel_support(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

    TermiosState ts;
    if (!terminal_begin_query_mode(&ts))
        return false;

    // XTSMGRAPHICS query: read sixel geometry (item 2).
    // Response: \x1b[?2;Ps2;...S — Ps2=0 means no error (sixel supported).
    // More reliable than DA1 attribute 4, which many non-sixel terminals also report.
    safe_write("\x1b[?2;1;0S", 9);

    char buffer[64];
    bool found = false;

    ssize_t r = terminal_read_query(buffer, sizeof(buffer) - 1, 'S');
    if (r > 0) {
        buffer[r] = '\0';
        char *p = strstr(buffer, "\x1b[?2;");
        char *endptr;
        if (p && strtol(p + 5, &endptr, 10) == 0 && endptr != (p + 5))
            found = true;
    }

    terminal_end_query_mode(&ts);
    return found;
}
