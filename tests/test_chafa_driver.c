#include "core/args.h"
#include "terminal/chafa_driver.h"
#include "terminal/terminal.h"

#include <unity.h>

void setUp(void) {}

void tearDown(void) {
    chafa_driver_cleanup();
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

static void test_explicit_flags_select_pixel_or_character_output(void) {
    Args args = {0};

    args.use_kitty = true;
    chafa_driver_configure(&args);
    TEST_ASSERT_FALSE(chafa_driver_uses_character_cells());

    args = (Args){.use_sixel = true};
    chafa_driver_configure(&args);
    TEST_ASSERT_FALSE(chafa_driver_uses_character_cells());

    args = (Args){.use_truecolor_characters = true};
    chafa_driver_configure(&args);
    TEST_ASSERT_TRUE(chafa_driver_uses_character_cells());

    args = (Args){.use_palette_characters = true};
    chafa_driver_configure(&args);
    TEST_ASSERT_TRUE(chafa_driver_uses_character_cells());

    args = (Args){.use_block_characters = true};
    chafa_driver_configure(&args);
    TEST_ASSERT_TRUE(chafa_driver_uses_character_cells());
}

static void test_character_modes_render_two_by_four_samples_per_cell(void) {
    uint32_t cols;
    uint32_t rows;
    uint32_t width;
    uint32_t height;
    get_terminal_size(&cols, &rows);

    calculate_render_dimensions(-1, -1, false, false, &width, &height);
    TEST_ASSERT_EQUAL_UINT32(cols * SYMBOL_CELL_SOURCE_WIDTH, width);
    TEST_ASSERT_EQUAL_UINT32(rows * SYMBOL_CELL_SOURCE_HEIGHT, height);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_chafa_sequences_are_parsed_inside_mixed_input);
    RUN_TEST(test_explicit_flags_select_pixel_or_character_output);
    RUN_TEST(test_character_modes_render_two_by_four_samples_per_cell);
    return UNITY_END();
}
