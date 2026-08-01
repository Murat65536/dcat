#include "terminal/chafa_driver.h"
#include "chafa.h"
#include "core/profiling.h"
#include "glib.h"
#include "terminal/terminal.h"

#include <string.h>

typedef struct {
    ChafaTermInfo *term_info;
    ChafaCanvas *canvas;
    ChafaPixelMode detected_pixel_mode;
    ChafaCanvasMode detected_canvas_mode;
    ChafaPixelMode pixel_mode;
    ChafaCanvasMode canvas_mode;
    uint32_t source_width;
    uint32_t source_height;
    bool sixel_scrolling_disabled;
} ChafaDriverState;

static ChafaDriverState g_state;

static void initialize(void) {
    chafa_set_n_threads(0);

    if (g_state.term_info) {
        return;
    }

    gchar **environment = g_get_environ();
    g_state.term_info = chafa_term_db_detect(chafa_term_db_get_default(), environment);
    g_strfreev(environment);

    g_state.detected_pixel_mode = chafa_term_info_get_best_pixel_mode(g_state.term_info);
    g_state.detected_canvas_mode = chafa_term_info_get_best_canvas_mode(g_state.term_info);

    g_state.pixel_mode = g_state.detected_pixel_mode;
    g_state.canvas_mode = g_state.detected_canvas_mode;
}

void chafa_driver_configure(const Args *args) {
    initialize();
    ChafaPixelMode pixel_mode = g_state.detected_pixel_mode;
    ChafaCanvasMode canvas_mode = g_state.detected_canvas_mode;

    if (args->use_kitty) {
        pixel_mode = CHAFA_PIXEL_MODE_KITTY;
        canvas_mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (args->use_sixel) {
        pixel_mode = CHAFA_PIXEL_MODE_SIXELS;
        canvas_mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (args->use_truecolor_characters) {
        pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
        canvas_mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (args->use_palette_characters) {
        pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
        canvas_mode = CHAFA_CANVAS_MODE_INDEXED_240;
    } else if (args->use_block_characters) {
        pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
        canvas_mode = CHAFA_CANVAS_MODE_FGBG;
    }

    if (g_state.pixel_mode != pixel_mode || g_state.canvas_mode != canvas_mode) {
        if (g_state.canvas) {
            chafa_canvas_unref(g_state.canvas);
            g_state.canvas = NULL;
        }
        g_state.pixel_mode = pixel_mode;
        g_state.canvas_mode = canvas_mode;
    }
}

void chafa_driver_invalidate_canvas(void) {
    g_clear_pointer(&g_state.canvas, chafa_canvas_unref);
}

ChafaTermInfo *chafa_driver_get_term_info(void) {
    initialize();
    return g_state.term_info;
}

bool chafa_driver_uses_character_cells(void) {
    initialize();
    return g_state.pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS;
}

static ChafaCanvas *create_canvas(const uint32_t width, const uint32_t height) {
    ChafaCanvasConfig *config = chafa_canvas_config_new();

    int canvas_width;
    int canvas_height;
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        ChafaSymbolMap *symbols = chafa_symbol_map_new();
        canvas_width = (int)((width + SYMBOL_CELL_SOURCE_WIDTH - 1U) / SYMBOL_CELL_SOURCE_WIDTH);
        canvas_height =
            (int)((height + SYMBOL_CELL_SOURCE_HEIGHT - 1U) / SYMBOL_CELL_SOURCE_HEIGHT);
        if (g_state.canvas_mode == CHAFA_CANVAS_MODE_FGBG) {
            chafa_symbol_map_add_by_tags(symbols, CHAFA_SYMBOL_TAG_VHALF);
        } else {
            const ChafaSymbolTags safe_graphics =
                chafa_term_info_get_safe_symbol_tags(g_state.term_info) &
                (CHAFA_SYMBOL_TAG_BLOCK | CHAFA_SYMBOL_TAG_BORDER | CHAFA_SYMBOL_TAG_BRAILLE);
            chafa_symbol_map_add_by_tags(symbols, CHAFA_SYMBOL_TAG_SPACE | CHAFA_SYMBOL_TAG_SOLID |
                                                      safe_graphics);
            chafa_symbol_map_remove_by_tags(symbols, CHAFA_SYMBOL_TAG_WIDE);
        }
        chafa_canvas_config_set_symbol_map(config, symbols);
        chafa_symbol_map_unref(symbols);
    } else {
        uint32_t terminal_cols;
        uint32_t terminal_rows;
        uint32_t terminal_pixel_width;
        uint32_t terminal_pixel_height;
        get_terminal_size(&terminal_cols, &terminal_rows);
        get_terminal_size_pixels(&terminal_pixel_width, &terminal_pixel_height);

        const uint32_t cell_width = terminal_cols > 0 ? terminal_pixel_width / terminal_cols : 0;
        const uint32_t cell_height = terminal_rows > 0 ? terminal_pixel_height / terminal_rows : 0;
        const uint32_t safe_cell_width = cell_width > 0 ? cell_width : 10;
        const uint32_t safe_cell_height = cell_height > 0 ? cell_height : 20;

        canvas_width = (int)((width + safe_cell_width - 1U) / safe_cell_width);
        canvas_height = (int)((height + safe_cell_height - 1U) / safe_cell_height);
        chafa_canvas_config_set_cell_geometry(config, (int)safe_cell_width, (int)safe_cell_height);
        if (chafa_term_info_get_is_pixel_passthrough_needed(g_state.term_info,
                                                            g_state.pixel_mode)) {
            chafa_canvas_config_set_passthrough(
                config, chafa_term_info_get_passthrough_type(g_state.term_info));
        }
    }

    chafa_canvas_config_set_geometry(config, canvas_width, canvas_height);
    chafa_canvas_config_set_pixel_mode(config, g_state.pixel_mode);
    chafa_canvas_config_set_canvas_mode(config, g_state.canvas_mode);
    chafa_canvas_config_set_work_factor(config, 0.0F);
    chafa_canvas_config_set_preprocessing_enabled(config, FALSE);

    ChafaCanvas *canvas = chafa_canvas_new(config);
    chafa_canvas_config_unref(config);
    return canvas;
}

static GString *build_frame(const uint8_t *framebuffer, const uint32_t width,
                            const uint32_t height) {
    initialize();
    if (!g_state.canvas || g_state.source_width != width || g_state.source_height != height) {
        chafa_driver_invalidate_canvas();
        g_state.canvas = create_canvas(width, height);
        g_state.source_width = width;
        g_state.source_height = height;
    }

    DCAT_PROFILE_ZONE(draw_zone, "Chafa draw pixels");
    chafa_canvas_draw_all_pixels(g_state.canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, framebuffer,
                                 (int)width, (int)height, (int)(width * 4U));
    DCAT_PROFILE_ZONE_END(draw_zone);

    DCAT_PROFILE_ZONE(print_zone, "Chafa print");
    GString *output = chafa_canvas_print(g_state.canvas, g_state.term_info);
    DCAT_PROFILE_ZONE_END(print_zone);
    return output;
}

void chafa_driver_render(const uint8_t *framebuffer, const uint32_t width, const uint32_t height) {
    initialize();
    char sequence[CHAFA_TERM_SEQ_LENGTH_MAX];
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SIXELS && !g_state.sixel_scrolling_disabled) {
        const char *end = chafa_term_info_emit_disable_sixel_scrolling(g_state.term_info, sequence);
        safe_write(sequence, (size_t)(end - sequence));
        g_state.sixel_scrolling_disabled = true;
    }
    GString *output = build_frame(framebuffer, width, height);
    if (!output) {
        return;
    }
    DCAT_PROFILE_PLOT_INT("Chafa output bytes", output->len);

    const char *end = chafa_term_info_emit_cursor_to_top_left(g_state.term_info, sequence);
    safe_write(sequence, (size_t)(end - sequence));
    safe_write(output->str, output->len);
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        end = chafa_term_info_emit_reset_attributes(g_state.term_info, sequence);
        safe_write(sequence, (size_t)(end - sequence));
    }
    g_string_free(output, true);
}

void chafa_driver_cleanup(void) {
    if (g_state.sixel_scrolling_disabled) {
        char sequence[CHAFA_TERM_SEQ_LENGTH_MAX];
        const char *end = chafa_term_info_emit_enable_sixel_scrolling(g_state.term_info, sequence);
        safe_write(sequence, (size_t)(end - sequence));
    }
    if (g_state.canvas) {
        chafa_canvas_unref(g_state.canvas);
    }
    if (g_state.term_info) {
        chafa_term_info_unref(g_state.term_info);
    }
    memset(&g_state, 0, sizeof(g_state));
}
