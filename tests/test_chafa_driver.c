#include "core/args.h"
#include "terminal/chafa_driver.h"
#include "terminal/terminal.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

static void test_frame_writer_preserves_order(void) {
    FILE *capture = tmpfile();
    TEST_ASSERT_NOT_NULL(capture);

#ifdef _WIN32
    const int saved_fd = _dup(STDOUT_FILENO);
    const HANDLE saved_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected =
        saved_fd >= 0 && _dup2(_fileno(capture), STDOUT_FILENO) == 0 &&
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(capture))) != 0;
#else
    const int saved_fd = dup(STDOUT_FILENO);
    const bool redirected = saved_fd >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0;
#endif

    if (redirected) {
        terminal_frame_begin();
        safe_write("a", 1);
        safe_write("b", 1);
        terminal_frame_end();
        terminal_frame_begin();
        safe_write("c", 1);
        terminal_frame_end();
        safe_write("d", 1);
        terminal_frame_writer_shutdown();
    }

    fflush(capture);
    rewind(capture);
    char output[5] = {0};
    const size_t output_size = fread(output, 1, sizeof(output) - 1U, capture);

#ifdef _WIN32
    SetStdHandle(STD_OUTPUT_HANDLE, saved_handle);
    if (saved_fd >= 0) {
        _dup2(saved_fd, STDOUT_FILENO);
        _close(saved_fd);
    }
#else
    if (saved_fd >= 0) {
        dup2(saved_fd, STDOUT_FILENO);
        close(saved_fd);
    }
#endif
    fclose(capture);

    TEST_ASSERT_TRUE(redirected);
    TEST_ASSERT_EQUAL_UINT(4, output_size);
    TEST_ASSERT_EQUAL_STRING("abcd", output);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_chafa_sequences_are_parsed_inside_mixed_input);
    RUN_TEST(test_explicit_flags_select_pixel_or_character_output);
    RUN_TEST(test_character_modes_render_two_by_four_samples_per_cell);
    RUN_TEST(test_frame_writer_preserves_order);
    return UNITY_END();
}
