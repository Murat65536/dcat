#include "core/args.h"
#include "terminal/chafa_driver.h"
#include "terminal/driver_factory.h"

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pixel_protocol_responses_are_detected);
    RUN_TEST(test_explicit_flags_keep_their_driver_names);
    return UNITY_END();
}
