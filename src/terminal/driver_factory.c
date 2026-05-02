#include "terminal/driver_factory.h"
#include "terminal/kitty.h"
#include "terminal/kitty_shm.h"
#include "terminal/sixel.h"
#include "terminal/block_characters.h"
#include "terminal/palette_characters.h"
#include "terminal/truecolor_characters.h"

#include <stddef.h>

static void wrapper_kitty_shm(const uint8_t *framebuffer, uint32_t width, uint32_t height, bool use_hash) {
    (void)use_hash;
    render_kitty_shm(framebuffer, width, height);
}

static void wrapper_kitty_direct(const uint8_t *framebuffer, uint32_t width, uint32_t height, bool use_hash) {
    (void)use_hash;
    render_kitty(framebuffer, width, height);
}

static void wrapper_sixel(const uint8_t *framebuffer, uint32_t width, uint32_t height, bool use_hash) {
    (void)use_hash;
    render_sixel(framebuffer, width, height);
}

static const OutputDriver g_driver_kitty_shm = {
    .name = "kitty_shm",
    .flag_name = "--kitty",
    .uses_kitty_protocol = true,
    .uses_character_cells = false,
    .detect_support = detect_kitty_shm_support,
    .render_frame = wrapper_kitty_shm,
};

static const OutputDriver g_driver_kitty_direct = {
    .name = "kitty_direct",
    .flag_name = "--kitty-direct",
    .uses_kitty_protocol = true,
    .uses_character_cells = false,
    .detect_support = detect_kitty_support,
    .render_frame = wrapper_kitty_direct,
};

static const OutputDriver g_driver_sixel = {
    .name = "sixel",
    .flag_name = "--sixel",
    .uses_kitty_protocol = false,
    .uses_character_cells = false,
    .detect_support = detect_sixel_support,
    .render_frame = wrapper_sixel,
};

static const OutputDriver g_driver_truecolor = {
    .name = "truecolor",
    .flag_name = "--truecolor-characters",
    .uses_kitty_protocol = false,
    .uses_character_cells = true,
    .detect_support = detect_truecolor_support,
    .render_frame = render_truecolor_characters,
};

static const OutputDriver g_driver_palette = {
    .name = "palette",
    .flag_name = "--palette-characters",
    .uses_kitty_protocol = false,
    .uses_character_cells = true,
    .detect_support = NULL,
    .render_frame = render_palette_characters,
};

static const OutputDriver g_driver_block = {
    .name = "block",
    .flag_name = "--block-characters",
    .uses_kitty_protocol = false,
    .uses_character_cells = true,
    .detect_support = NULL,
    .render_frame = render_block_characters,
};

const OutputDriver* driver_factory_get(const Args *args) {
    if (args->use_kitty_shm) return &g_driver_kitty_shm;
    if (args->use_kitty) return &g_driver_kitty_direct;
    if (args->use_sixel) return &g_driver_sixel;
    if (args->use_truecolor_characters) return &g_driver_truecolor;
    if (args->use_palette_characters) return &g_driver_palette;
    if (args->use_block_characters) return &g_driver_block;

    if (g_driver_kitty_shm.detect_support()) return &g_driver_kitty_shm;
    if (g_driver_kitty_direct.detect_support()) return &g_driver_kitty_direct;
    if (g_driver_sixel.detect_support()) return &g_driver_sixel;
    if (g_driver_truecolor.detect_support()) return &g_driver_truecolor;
    
    return &g_driver_palette;
}

bool driver_is_supported_on_platform(const OutputDriver *driver) {
#ifdef _WIN32
    if (driver == &g_driver_kitty_shm || driver == &g_driver_kitty_direct) {
        return false;
    }
#endif
    return true;
}
