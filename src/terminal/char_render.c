#include "terminal/char_render.h"
#include "terminal/terminal.h"
#include <stdlib.h>
#include <string.h>

static char u8_3digit[256][3];
static bool u8_table_initialized = false;

static void init_u8_table(void) {
    if (u8_table_initialized) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        u8_3digit[i][0] = '0' + (i / 100);
        u8_3digit[i][1] = '0' + ((i / 10) % 10);
        u8_3digit[i][2] = '0' + (i % 10);
    }
    u8_table_initialized = true;
}

const char *char_u8_3digit(uint8_t v) {
    init_u8_table();
    return u8_3digit[v];
}

static bool ensure_buf(CharRenderState *state, size_t needed) {
    if (state->buf_size >= needed) {
        return true;
    }
    free(state->buf);
    state->buf = (char *)malloc(needed);
    state->buf_size = state->buf ? needed : 0;
    return state->buf != NULL;
}

void render_color_characters(const uint8_t *buffer, const uint32_t width, const uint32_t height,
                             const bool use_hash, const CharCellCodec *codec, CharRenderState *st) {
    init_u8_table();

    if (use_hash) {
        const size_t num_cells = (size_t)width * height;
        const size_t needed = (sizeof(TERM_FRAME_BEGIN) - 1) + (num_cells * codec->hash_cell_len) +
                              (height > 0 ? height - 1 : 0) + (sizeof(TERM_COLOR_FRAME_END) - 1);
        if (!ensure_buf(st, needed)) {
            return;
        }

        char *p = st->buf;
        memcpy(p, TERM_FRAME_BEGIN, sizeof(TERM_FRAME_BEGIN) - 1);
        p += sizeof(TERM_FRAME_BEGIN) - 1;

        for (uint32_t y = 0; y < height; y++) {
            const uint8_t *row = buffer + ((size_t)y * width * 4);
            for (uint32_t x = 0; x < width; x++) {
                memcpy(p, codec->hash_cell_template, codec->hash_cell_len);
                codec->emit_hash(p, row[0], row[1], row[2]);
                p += codec->hash_cell_len;
                row += 4;
            }
            if (y + 1 < height) {
                *p++ = '\n';
            }
        }

        memcpy(p, TERM_COLOR_FRAME_END, sizeof(TERM_COLOR_FRAME_END) - 1);
        p += sizeof(TERM_COLOR_FRAME_END) - 1;
        safe_write(st->buf, (size_t)(p - st->buf));
        return;
    }

    const uint32_t num_blocks = width * ((height + 1) / 2);
    const size_t frame_size = (sizeof(TERM_FRAME_BEGIN) - 1) +
                              ((size_t)num_blocks * codec->block_len) +
                              (sizeof(TERM_COLOR_FRAME_END) - 1);

    // Rebuild the fixed cell structure only on first run or resize; steady-state
    // frames patch the per-pixel digits in place below.
    if (!st->initialized || width != st->last_width || height != st->last_height) {
        if (!ensure_buf(st, frame_size)) {
            return;
        }

        char *p = st->buf;
        memcpy(p, TERM_FRAME_BEGIN, sizeof(TERM_FRAME_BEGIN) - 1);
        p += sizeof(TERM_FRAME_BEGIN) - 1;
        for (uint32_t i = 0; i < num_blocks; i++) {
            memcpy(p, codec->block_template, codec->block_len);
            p += codec->block_len;
        }
        memcpy(p, TERM_COLOR_FRAME_END, sizeof(TERM_COLOR_FRAME_END) - 1);

        st->last_width = width;
        st->last_height = height;
        st->initialized = true;
    }

    char *block_ptr = st->buf + (sizeof(TERM_FRAME_BEGIN) - 1);
    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t *row_upper = buffer + ((size_t)y * width * 4);
        const uint8_t *row_lower = buffer + ((size_t)(y + 1) * width * 4);
        const bool has_lower = (y + 1 < height);

        for (uint32_t x = 0; x < width; x++) {
            const uint8_t ru = row_upper[0];
            const uint8_t gu = row_upper[1];
            const uint8_t bu = row_upper[2];
            row_upper += 4;

            uint8_t rl = 0;
            uint8_t gl = 0;
            uint8_t bl = 0;
            if (has_lower) {
                rl = row_lower[0];
                gl = row_lower[1];
                bl = row_lower[2];
                row_lower += 4;
            }

            codec->emit_block(block_ptr, ru, gu, bu, rl, gl, bl, has_lower);
            block_ptr += codec->block_len;
        }
    }

    safe_write(st->buf, frame_size);
}
