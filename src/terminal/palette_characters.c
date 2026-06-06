#include "terminal/palette_characters.h"
#include "terminal/char_render.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t rgb_to_256(const uint8_t r, const uint8_t g, const uint8_t b) {
    // Check if grayscale
    if (r == g && g == b) {
        if (r < 8)
            return 16;
        if (r > 248)
            return 231;
        return 232 + (r - 8) / 10;
    }

    // 6x6x6 color cube
    int vr = (r <= 47) ? 0 : (r - 55) / 40 + 1;
    int vg = (g <= 47) ? 0 : (g - 55) / 40 + 1;
    int vb = (b <= 47) ? 0 : (b - 55) / 40 + 1;

    if (vr > 5)
        vr = 5;
    if (vg > 5)
        vg = 5;
    if (vb > 5)
        vb = 5;

    return 16 + 36 * vr + 6 * vg + vb;
}

// Hash cell: \x1b[38;5;000m# = 12 bytes; palette index at offset 7.
static void emit_hash(char *cell, uint8_t r, uint8_t g, uint8_t b) {
    memcpy(cell + 7, char_u8_3digit(rgb_to_256(r, g, b)), 3);
}

// Block cell: \x1b[38;5;000;48;5;000m▀ = 23 bytes; foreground index at 7,
// background index at 16 (index 0 when the lower source row is absent).
static void emit_block(char *block, uint8_t ru, uint8_t gu, uint8_t bu, uint8_t rl, uint8_t gl,
                       uint8_t bl, bool has_lower) {
    memcpy(block + 7, char_u8_3digit(rgb_to_256(ru, gu, bu)), 3);
    const uint8_t idx_lower = has_lower ? rgb_to_256(rl, gl, bl) : 0;
    memcpy(block + 16, char_u8_3digit(idx_lower), 3);
}

static const CharCellCodec palette_codec = {
    .hash_cell_len = 12,
    .hash_cell_template = "\x1b[38;5;000m#",
    .emit_hash = emit_hash,
    .block_len = 23,
    .block_template = "\x1b[38;5;000;48;5;000m\xe2\x96\x80",
    .emit_block = emit_block,
};

void render_palette_characters(const uint8_t *buffer, const uint32_t width, const uint32_t height,
                               const bool use_hash_characters) {
    static CharRenderState state;
    render_color_characters(buffer, width, height, use_hash_characters, &palette_codec, &state);
}
