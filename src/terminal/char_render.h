#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Shared terminal control sequences for the character renderers.
// FRAME_BEGIN is identical for color and luminance renderers; SYNC_END closes
// synchronized output. Color renderers also reset SGR state via COLOR_FRAME_END.
#ifdef _WIN32
#define TERM_FRAME_BEGIN "\x1b[H"
#define TERM_SYNC_END ""
#else
#define TERM_FRAME_BEGIN "\x1b[?2026h\x1b[H"
#define TERM_SYNC_END "\x1b[?2026l"
#endif
#define TERM_COLOR_FRAME_END "\x1b[0m" TERM_SYNC_END

// Pointer to a fixed 3-byte zero-padded decimal string for v (no terminator).
const char *char_u8_3digit(uint8_t v);

// Per-renderer persistent output buffer + cached frame geometry.
typedef struct CharRenderState {
    char *buf;
    size_t buf_size;
    uint32_t last_width;
    uint32_t last_height;
    bool initialized;
} CharRenderState;

// Describes how one fixed-width color renderer encodes a cell. emit_hash patches
// one pixel into a hash-mode cell; emit_block patches an upper/lower pixel pair
// into a half-block cell. When has_lower is false the lower pixel is absent (its
// rgb is 0,0,0) and the codec should encode the cell's "no lower row" placeholder.
typedef struct CharCellCodec {
    int hash_cell_len;
    const char *hash_cell_template;
    void (*emit_hash)(char *cell, uint8_t r, uint8_t g, uint8_t b);

    int block_len;
    const char *block_template;
    void (*emit_block)(char *block, uint8_t ru, uint8_t gu, uint8_t bu, uint8_t rl, uint8_t gl,
                       uint8_t bl, bool has_lower);
} CharCellCodec;

// Renders an RGBA framebuffer as fixed-width colored character cells, reusing the
// state's buffer (and, in half-block mode, its prebuilt cell structure) across frames.
void render_color_characters(const uint8_t *buffer, uint32_t width, uint32_t height, bool use_hash,
                             const CharCellCodec *codec, CharRenderState *state);
