#include "sixel.h"
#include "terminal.h"
#include <sixel.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int sixel_write_cb(char *data, int size, void *priv) {
    (void)priv;
    safe_write(data, (size_t)size);
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

bool detect_sixel_support(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

    TermiosState ts;
    if (!termios_state_init(&ts, STDIN_FILENO))
        return false;
    ts.settings.c_lflag &= ~(ICANON | ECHO);
    ts.settings.c_cc[VMIN] = 0;
    ts.settings.c_cc[VTIME] = 1; // 100ms timeout
    if (!termios_state_apply(&ts))
        return false;

    // XTSMGRAPHICS query: read sixel geometry (item 2).
    // Response: \x1b[?2;Ps2;...S — Ps2=0 means no error (sixel supported).
    // More reliable than DA1 attribute 4, which many non-sixel terminals also report.
    safe_write("\x1b[?2;1;0S", 9);

    char buffer[64];
    bool found = false;

    ssize_t r = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
    if (r > 0) {
        buffer[r] = '\0';
        char *p = strstr(buffer, "\x1b[?2;");
        if (p && atoi(p + 5) == 0)
            found = true;
    }

    termios_state_restore(&ts);
    return found;
}
