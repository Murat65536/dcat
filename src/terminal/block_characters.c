#include "terminal/block_characters.h"
#include "terminal/terminal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Persistent buffer for variable-length output
static char *render_buf = NULL;
static size_t render_buf_size = 0;

#ifdef _WIN32
static const char FRAME_BEGIN[] = "\x1b[H";
static const char FRAME_END[] = "";
#else
static const char FRAME_BEGIN[] = "\x1b[?2026h\x1b[H";
static const char FRAME_END[] = "\x1b[?2026l";
#endif

#define LUMA_THRESHOLD 63

static inline uint8_t luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((r * 77u + g * 150u + b * 29u) >> 8);
}

void render_block_characters(const uint8_t *buffer, uint32_t width, uint32_t height,
                             bool use_hash_characters) {
    if (use_hash_characters) {
        size_t max_size = (sizeof(FRAME_BEGIN) - 1) + (size_t)height * width +
                          (height > 0 ? height - 1 : 0) + (sizeof(FRAME_END) - 1);

        if (render_buf_size < max_size) {
            free(render_buf);
            render_buf = (char *)malloc(max_size);
            render_buf_size = max_size;
        }

        char *p = render_buf;
        memcpy(p, FRAME_BEGIN, sizeof(FRAME_BEGIN) - 1);
        p += sizeof(FRAME_BEGIN) - 1;

        for (uint32_t y = 0; y < height; y++) {
            const uint8_t *row = buffer + (y * width * 4);
            for (uint32_t x = 0; x < width; x++) {
                uint8_t luma = luminance(row[0], row[1], row[2]);
                *p++ = (luma >= LUMA_THRESHOLD) ? '#' : ' ';
                row += 4;
            }
            if (y + 1 < height) {
                *p++ = '\n';
            }
        }

        memcpy(p, FRAME_END, sizeof(FRAME_END) - 1);
        p += sizeof(FRAME_END) - 1;
        safe_write(render_buf, (size_t)(p - render_buf));
        return;
    }

    uint32_t rows = (height + 1) / 2;
    // Worst case: every cell is a 3-byte block char
    // Header(12) + rows * width * 3 + newlines(rows-1) + footer(9)
    size_t max_size = (sizeof(FRAME_BEGIN) - 1) + (size_t)rows * width * 3 +
                      (rows > 0 ? rows - 1 : 0) + (sizeof(FRAME_END) - 1);

    if (render_buf_size < max_size) {
        free(render_buf);
        render_buf = (char *)malloc(max_size);
        render_buf_size = max_size;
    }

    char *p = render_buf;

    // Header: enable synchronized output + cursor home
    memcpy(p, FRAME_BEGIN, sizeof(FRAME_BEGIN) - 1);
    p += sizeof(FRAME_BEGIN) - 1;

    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t *row_upper = buffer + (y * width * 4);
        const uint8_t *row_lower = buffer + ((y + 1) * width * 4);
        bool has_lower = (y + 1 < height);

        for (uint32_t x = 0; x < width; x++) {
            uint8_t lU = luminance(row_upper[0], row_upper[1], row_upper[2]);
            row_upper += 4;

            uint8_t lL = 0;
            if (has_lower) {
                lL = luminance(row_lower[0], row_lower[1], row_lower[2]);
                row_lower += 4;
            }

            bool upper_on = lU >= LUMA_THRESHOLD;
            bool lower_on = lL >= LUMA_THRESHOLD;

            if (upper_on && lower_on) {
                memcpy(p, "\xe2\x96\x88", 3);
                p += 3;
            } else if (upper_on) {
                memcpy(p, "\xe2\x96\x80", 3);
                p += 3;
            } else if (lower_on) {
                memcpy(p, "\xe2\x96\x84", 3);
                p += 3;
            } else {
                *p++ = ' ';
            }
        }

        if (y + 2 < height) {
            *p++ = '\n';
        }
    }

    // Footer: disable synchronized output
    memcpy(p, FRAME_END, sizeof(FRAME_END) - 1);
    p += sizeof(FRAME_END) - 1;

    safe_write(render_buf, (size_t)(p - render_buf));
}
