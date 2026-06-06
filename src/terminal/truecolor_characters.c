#include "terminal/truecolor_characters.h"
#include "platform/io.h"
#include "terminal/char_render.h"
#include "terminal/terminal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Hash cell: \x1b[38;2;RRR;GGG;BBBm# = 18 bytes; RGB at offsets 7/11/15.
static void emit_hash(char *cell, uint8_t r, uint8_t g, uint8_t b) {
    memcpy(cell + 7, char_u8_3digit(r), 3);
    memcpy(cell + 11, char_u8_3digit(g), 3);
    memcpy(cell + 15, char_u8_3digit(b), 3);
}

// Block cell: \x1b[38;2;RRR;GGG;BBB;48;2;RRR;GGG;BBBm▀ = 39 bytes;
// foreground RGB at 7/11/15, background RGB at 24/28/32.
static void emit_block(char *block, uint8_t ru, uint8_t gu, uint8_t bu, uint8_t rl, uint8_t gl,
                       uint8_t bl, bool has_lower) {
    (void)has_lower;
    memcpy(block + 7, char_u8_3digit(ru), 3);
    memcpy(block + 11, char_u8_3digit(gu), 3);
    memcpy(block + 15, char_u8_3digit(bu), 3);
    memcpy(block + 24, char_u8_3digit(rl), 3);
    memcpy(block + 28, char_u8_3digit(gl), 3);
    memcpy(block + 32, char_u8_3digit(bl), 3);
}

static const CharCellCodec truecolor_codec = {
    .hash_cell_len = 18,
    .hash_cell_template = "\x1b[38;2;000;000;000m#",
    .emit_hash = emit_hash,
    .block_len = 39,
    .block_template = "\x1b[38;2;000;000;000;48;2;000;000;000m\xe2\x96\x80",
    .emit_block = emit_block,
};

void render_truecolor_characters(const uint8_t *buffer, const uint32_t width, const uint32_t height,
                                 const bool use_hash_characters) {
    static CharRenderState state;
    render_color_characters(buffer, width, height, use_hash_characters, &truecolor_codec, &state);
}

bool detect_truecolor_support(void) {
#ifdef _WIN32
    if (!dcat_isatty(STDOUT_FILENO)) {
        return false;
    }

    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        return true;
    }

    const char *wt_session = getenv("WT_SESSION");
    if (wt_session && wt_session[0]) {
        return true;
    }

    return true;
#else
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        return true;
    }

    const char *term = getenv("TERM");
    if (term && (strstr(term, "iterm") || strstr(term, "konsole") || strstr(term, "st-256color"))) {
        return true;
    }

    // Fallback: Query terminal for RGB capability: DCS + q 524742 ST
    if (!dcat_isatty(STDOUT_FILENO) || !dcat_isatty(STDIN_FILENO))
        return false;

    TermiosState ts;
    if (!terminal_begin_query_mode(&ts))
        return false;

    // Query RGB capability (524742 is hex for "RGB")
    safe_write("\x1bP+q524742\x1b\\", 11);

    char response[128];
    bool found = false;
    ssize_t r = terminal_read_query(response, sizeof(response) - 1, '\\');
    if (r > 0) {
        response[r] = '\0';
        // Success response: DCS 1 + r 524742=... ST
        if (strstr(response, "1+r524742")) {
            found = true;
        }
    }

    terminal_end_query_mode(&ts);
    return found;
#endif
}
