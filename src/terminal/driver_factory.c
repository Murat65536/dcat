#include "terminal/driver_factory.h"
#include "terminal/chafa_driver.h"
#ifndef _WIN32
#include "terminal/kitty_shm.h"
#endif

#include <stddef.h>

#ifndef _WIN32
static const OutputDriver g_driver_kitty_shm = {
    .name = "kitty_shm",
    .uses_character_cells = false,
    .render_frame = render_kitty_shm,
};
#endif

static const OutputDriver g_driver_kitty_direct = {
    .name = "kitty_direct",
    .uses_character_cells = false,
    .render_frame = chafa_driver_render,
};

static const OutputDriver g_driver_sixel = {
    .name = "sixel",
    .uses_character_cells = false,
    .render_frame = chafa_driver_render,
};

static const OutputDriver g_driver_iterm2 = {
    .name = "iterm2",
    .uses_character_cells = false,
    .render_frame = chafa_driver_render,
};

static const OutputDriver g_driver_truecolor = {
    .name = "truecolor",
    .uses_character_cells = true,
    .render_frame = chafa_driver_render,
};

static const OutputDriver g_driver_palette = {
    .name = "palette",
    .uses_character_cells = true,
    .render_frame = chafa_driver_render,
};

static const OutputDriver g_driver_block = {
    .name = "block",
    .uses_character_cells = true,
    .render_frame = chafa_driver_render,
};

static const OutputDriver *select_chafa(const OutputDriver *driver, const ChafaPixelMode pixel_mode,
                                        const ChafaCanvasMode canvas_mode) {
    chafa_driver_configure(pixel_mode, canvas_mode);
    return driver;
}

const OutputDriver *driver_factory_get(const Args *args) {
    if (args->use_kitty_shm) {
#ifdef _WIN32
        return select_chafa(&g_driver_kitty_direct, CHAFA_PIXEL_MODE_KITTY,
                            CHAFA_CANVAS_MODE_TRUECOLOR);
#else
        return &g_driver_kitty_shm;
#endif
    }
    if (args->use_kitty) {
        return select_chafa(&g_driver_kitty_direct, CHAFA_PIXEL_MODE_KITTY,
                            CHAFA_CANVAS_MODE_TRUECOLOR);
    }
    if (args->use_sixel) {
        return select_chafa(&g_driver_sixel, CHAFA_PIXEL_MODE_SIXELS, CHAFA_CANVAS_MODE_TRUECOLOR);
    }
    if (args->use_truecolor_characters) {
        return select_chafa(&g_driver_truecolor, CHAFA_PIXEL_MODE_SYMBOLS,
                            CHAFA_CANVAS_MODE_TRUECOLOR);
    }
    if (args->use_palette_characters) {
        return select_chafa(&g_driver_palette, CHAFA_PIXEL_MODE_SYMBOLS,
                            CHAFA_CANVAS_MODE_INDEXED_240);
    }
    if (args->use_block_characters) {
        return select_chafa(&g_driver_block, CHAFA_PIXEL_MODE_SYMBOLS, CHAFA_CANVAS_MODE_FGBG);
    }

    ChafaPixelMode pixel_mode;
    ChafaCanvasMode canvas_mode;
    chafa_driver_detect(&pixel_mode, &canvas_mode);

#ifndef _WIN32
    const char *kitty_pid = getenv("KITTY_PID");
    if (pixel_mode == CHAFA_PIXEL_MODE_KITTY && kitty_pid && kitty_pid[0]) {
        return &g_driver_kitty_shm;
    }
#endif

    switch (pixel_mode) {
    case CHAFA_PIXEL_MODE_KITTY:
        return select_chafa(&g_driver_kitty_direct, pixel_mode, canvas_mode);
    case CHAFA_PIXEL_MODE_SIXELS:
        return select_chafa(&g_driver_sixel, pixel_mode, canvas_mode);
    case CHAFA_PIXEL_MODE_ITERM2:
        return select_chafa(&g_driver_iterm2, pixel_mode, canvas_mode);
    case CHAFA_PIXEL_MODE_SYMBOLS:
    case CHAFA_PIXEL_MODE_MAX:
        break;
    }

    if (canvas_mode == CHAFA_CANVAS_MODE_TRUECOLOR) {
        return select_chafa(&g_driver_truecolor, pixel_mode, canvas_mode);
    }
    if (canvas_mode != CHAFA_CANVAS_MODE_FGBG && canvas_mode != CHAFA_CANVAS_MODE_FGBG_BGFG) {
        return select_chafa(&g_driver_palette, pixel_mode, canvas_mode);
    }
    return select_chafa(&g_driver_block, pixel_mode, canvas_mode);
}
