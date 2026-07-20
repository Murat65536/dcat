#include "terminal/chafa_driver.h"
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
    bool use_hash_characters;
    bool initialized;
    bool sixel_scrolling_disabled;
} ChafaDriverState;

static ChafaDriverState g_state;

ChafaPixelMode chafa_driver_pixel_mode_from_response(const char *response) {
    if (strstr(response, "\x1b_Gi=31;OK")) {
        return CHAFA_PIXEL_MODE_KITTY;
    }

    const char *parameters = strstr(response, "\x1b[?");
    if (!parameters) {
        return CHAFA_PIXEL_MODE_SYMBOLS;
    }
    parameters += 3;

    while (*parameters) {
        unsigned int value = 0;
        if (*parameters < '0' || *parameters > '9') {
            return CHAFA_PIXEL_MODE_SYMBOLS;
        }
        while (*parameters >= '0' && *parameters <= '9') {
            value = value * 10U + (unsigned int)(*parameters - '0');
            parameters++;
        }
        if (value == 4U) {
            return *parameters == ';' || *parameters == 'c' ? CHAFA_PIXEL_MODE_SIXELS
                                                            : CHAFA_PIXEL_MODE_SYMBOLS;
        }
        if (*parameters == 'c') {
            return CHAFA_PIXEL_MODE_SYMBOLS;
        }
        if (*parameters++ != ';') {
            return CHAFA_PIXEL_MODE_SYMBOLS;
        }
    }
    return CHAFA_PIXEL_MODE_SYMBOLS;
}

static ChafaPixelMode probe_pixel_mode(void) {
    if (!dcat_isatty(STDIN_FILENO) || !dcat_isatty(STDOUT_FILENO)) {
        return CHAFA_PIXEL_MODE_SYMBOLS;
    }

    TermiosState state;
    if (!terminal_begin_query_mode(&state)) {
        return CHAFA_PIXEL_MODE_SYMBOLS;
    }
    static const char query[] = "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[0c";
    safe_write(query, sizeof(query) - 1U);
    char response[256];
    const ssize_t length = terminal_read_query(response, sizeof(response) - 1U, 0);
    terminal_end_query_mode(&state);
    if (length <= 0) {
        return CHAFA_PIXEL_MODE_SYMBOLS;
    }
    response[length] = '\0';
    return chafa_driver_pixel_mode_from_response(response);
}

static void initialize(void) {
    if (g_state.initialized) {
        return;
    }

    gchar **environment = g_get_environ();
    g_state.term_info = chafa_term_db_detect(chafa_term_db_get_default(), environment);
    g_strfreev(environment);

    g_state.detected_pixel_mode = chafa_term_info_get_best_pixel_mode(g_state.term_info);
    g_state.detected_canvas_mode = chafa_term_info_get_best_canvas_mode(g_state.term_info);

    if (g_state.detected_pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        g_state.detected_pixel_mode = probe_pixel_mode();
    }

    ChafaTermInfo *fallback = chafa_term_db_get_fallback_info(chafa_term_db_get_default());
    chafa_term_info_supplement(g_state.term_info, fallback);
    chafa_term_info_unref(fallback);

    g_state.pixel_mode = g_state.detected_pixel_mode;
    g_state.canvas_mode = g_state.detected_canvas_mode;
    g_state.initialized = true;
}

void chafa_driver_detect(ChafaPixelMode *pixel_mode, ChafaCanvasMode *canvas_mode) {
    initialize();
    *pixel_mode = g_state.detected_pixel_mode;
    *canvas_mode = g_state.detected_canvas_mode;
}

void chafa_driver_configure(const ChafaPixelMode pixel_mode, const ChafaCanvasMode canvas_mode) {
    initialize();
    if (g_state.pixel_mode != pixel_mode || g_state.canvas_mode != canvas_mode) {
        if (g_state.canvas) {
            chafa_canvas_unref(g_state.canvas);
            g_state.canvas = NULL;
        }
        g_state.pixel_mode = pixel_mode;
        g_state.canvas_mode = canvas_mode;
    }
}

static ChafaCanvas *create_canvas(const uint32_t width, const uint32_t height,
                                  const bool use_hash_characters) {
    ChafaCanvasConfig *config = chafa_canvas_config_new();

    int canvas_width;
    int canvas_height;
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        ChafaSymbolMap *symbols = chafa_symbol_map_new();
        canvas_width = (int)width;
        canvas_height = (int)(use_hash_characters ? height : (height + 1U) / 2U);
        if (use_hash_characters) {
            chafa_symbol_map_add_by_range(symbols, ' ', ' ');
            chafa_symbol_map_add_by_range(symbols, '#', '#');
        } else {
            chafa_symbol_map_add_by_tags(symbols, CHAFA_SYMBOL_TAG_SPACE | CHAFA_SYMBOL_TAG_SOLID |
                                                      CHAFA_SYMBOL_TAG_VHALF);
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
    chafa_canvas_config_set_transparency_threshold(config, 1.0F);

    ChafaCanvas *canvas = chafa_canvas_new(config);
    chafa_canvas_config_unref(config);
    return canvas;
}

static GString *build_frame(const uint8_t *framebuffer, const uint32_t width, const uint32_t height,
                            const bool use_hash_characters) {
    initialize();
    if (!g_state.canvas || g_state.source_width != width || g_state.source_height != height ||
        g_state.use_hash_characters != use_hash_characters) {
        if (g_state.canvas) {
            chafa_canvas_unref(g_state.canvas);
        }
        g_state.canvas = create_canvas(width, height, use_hash_characters);
        g_state.source_width = width;
        g_state.source_height = height;
        g_state.use_hash_characters = use_hash_characters;
    }

    chafa_canvas_draw_all_pixels(g_state.canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, framebuffer,
                                 (int)width, (int)height, (int)(width * 4U));
    return chafa_canvas_print(g_state.canvas, g_state.term_info);
}

void chafa_driver_render(const uint8_t *framebuffer, const uint32_t width, const uint32_t height,
                         const bool use_hash_characters) {
    initialize();
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SIXELS && !g_state.sixel_scrolling_disabled) {
        safe_write("\x1b[?80h", 6);
        g_state.sixel_scrolling_disabled = true;
    }
    GString *output = build_frame(framebuffer, width, height, use_hash_characters);
    if (!output) {
        return;
    }

    safe_write("\x1b[H", 3);
    safe_write(output->str, output->len);
    if (g_state.pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        safe_write("\x1b[0m", 4);
    }
    g_string_free(output, true);
}

void chafa_driver_cleanup(void) {
    if (g_state.sixel_scrolling_disabled) {
        safe_write("\x1b[?80l", 6);
    }
    if (g_state.canvas) {
        chafa_canvas_unref(g_state.canvas);
    }
    if (g_state.term_info) {
        chafa_term_info_unref(g_state.term_info);
    }
    memset(&g_state, 0, sizeof(g_state));
}
