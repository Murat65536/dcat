#include "core/args.h"
#include "terminal/chafa_driver.h"
#include "terminal/driver_factory.h"
#include "terminal/terminal.h"

#include <unity.h>

void setUp(void) {}

void tearDown(void) {
    chafa_driver_cleanup();
}

static void test_pixel_protocol_responses_are_detected(void) {
    TEST_ASSERT_EQUAL_INT(CHAFA_PIXEL_MODE_KITTY,
                          chafa_driver_pixel_mode_from_response("\x1b_Gi=31;OK\x1b\\"));
    TEST_ASSERT_EQUAL_INT(CHAFA_PIXEL_MODE_SIXELS,
                          chafa_driver_pixel_mode_from_response("\x1b[?61;4;6;7;14;21;22c"));
    TEST_ASSERT_EQUAL_INT(CHAFA_PIXEL_MODE_SYMBOLS,
                          chafa_driver_pixel_mode_from_response("\x1b[?61;6;7;14;21;22c"));
    TEST_ASSERT_EQUAL_INT(CHAFA_PIXEL_MODE_SYMBOLS,
                          chafa_driver_pixel_mode_from_response("\x1b[?61;40;6c"));
}

static void test_chafa_sequences_are_parsed_inside_mixed_input(void) {
    ChafaTermInfo *term_info = chafa_term_db_get_fallback_info(chafa_term_db_get_default());
    static const char response[] = "noise\x1b[4;1080;1920tmore";
    guint args[CHAFA_TERM_SEQ_ARGS_MAX];
    gint n_args = 0;

    TEST_ASSERT_TRUE(terminal_parse_sequence(term_info, CHAFA_TERM_SEQ_TEXT_AREA_SIZE_PX, response,
                                             sizeof(response) - 1U, args, &n_args));
    TEST_ASSERT_EQUAL_INT(2, n_args);
    TEST_ASSERT_EQUAL_UINT(1080, args[0]);
    TEST_ASSERT_EQUAL_UINT(1920, args[1]);
    chafa_term_info_unref(term_info);
}

static void test_explicit_flags_keep_their_driver_names(void) {
    Args args = {0};

    args.use_kitty = true;
    TEST_ASSERT_EQUAL_STRING("kitty_direct", driver_factory_get(&args)->name);
#ifdef _WIN32
    args = (Args){.use_kitty_shm = true};
    TEST_ASSERT_EQUAL_STRING("kitty_direct", driver_factory_get(&args)->name);
#endif
    args = (Args){.use_sixel = true};
    TEST_ASSERT_EQUAL_STRING("sixel", driver_factory_get(&args)->name);
    args = (Args){.use_truecolor_characters = true};
    TEST_ASSERT_EQUAL_STRING("truecolor", driver_factory_get(&args)->name);
    args = (Args){.use_palette_characters = true};
    TEST_ASSERT_EQUAL_STRING("palette", driver_factory_get(&args)->name);
    args = (Args){.use_block_characters = true};
    TEST_ASSERT_EQUAL_STRING("block", driver_factory_get(&args)->name);
}

static void test_noise_dithering_is_limited_to_quantized_output(void) {
    static const ChafaCanvasMode indexed_modes[] = {
        CHAFA_CANVAS_MODE_INDEXED_256, CHAFA_CANVAS_MODE_INDEXED_240, CHAFA_CANVAS_MODE_INDEXED_16,
        CHAFA_CANVAS_MODE_INDEXED_8, CHAFA_CANVAS_MODE_INDEXED_16_8};

    TEST_ASSERT_EQUAL_INT(
        CHAFA_DITHER_MODE_NOISE,
        chafa_driver_dither_mode(CHAFA_PIXEL_MODE_SIXELS, CHAFA_CANVAS_MODE_TRUECOLOR));
    for (size_t i = 0; i < sizeof(indexed_modes) / sizeof(indexed_modes[0]); i++) {
        TEST_ASSERT_EQUAL_INT(CHAFA_DITHER_MODE_NOISE,
                              chafa_driver_dither_mode(CHAFA_PIXEL_MODE_SYMBOLS, indexed_modes[i]));
    }
    TEST_ASSERT_EQUAL_INT(
        CHAFA_DITHER_MODE_NONE,
        chafa_driver_dither_mode(CHAFA_PIXEL_MODE_SYMBOLS, CHAFA_CANVAS_MODE_TRUECOLOR));
    TEST_ASSERT_EQUAL_INT(CHAFA_DITHER_MODE_NONE, chafa_driver_dither_mode(CHAFA_PIXEL_MODE_SYMBOLS,
                                                                           CHAFA_CANVAS_MODE_FGBG));
}

static void test_character_modes_render_two_by_four_samples_per_cell(void) {
    uint32_t cols;
    uint32_t rows;
    uint32_t width;
    uint32_t height;
    get_terminal_size(&cols, &rows);

    calculate_render_dimensions(-1, -1, false, false, false, &width, &height);
    TEST_ASSERT_EQUAL_UINT32(cols * SYMBOL_CELL_SOURCE_WIDTH, width);
    TEST_ASSERT_EQUAL_UINT32(rows * SYMBOL_CELL_SOURCE_HEIGHT, height);

    calculate_render_dimensions(-1, -1, false, true, false, &width, &height);
    TEST_ASSERT_EQUAL_UINT32(cols, width);
    TEST_ASSERT_EQUAL_UINT32(rows, height);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pixel_protocol_responses_are_detected);
    RUN_TEST(test_chafa_sequences_are_parsed_inside_mixed_input);
    RUN_TEST(test_explicit_flags_keep_their_driver_names);
    RUN_TEST(test_noise_dithering_is_limited_to_quantized_output);
    RUN_TEST(test_character_modes_render_two_by_four_samples_per_cell);
    return UNITY_END();
}
