#include "input/input_handler.h"

#include <unity.h>

void setUp(void) {}

void tearDown(void) {}

static void test_character_cell_delta_uses_raw_sensitivity(void) {
    Camera camera = {.position = {0.0F, 0.0F, 1.0F}};
    InputThreadData data = {
        .camera = &camera,
        .mouse_orbit = true,
        .mouse_sensitivity = 0.02F,
    };
    MouseTracker tracker = {0};
    size_t consumed = 0;

    TEST_ASSERT_EQUAL_INT(INPUT_CSI_HANDLED,
                          input_parse_csi(&data, "<0;10;10M", 10, &consumed, &tracker));
    TEST_ASSERT_EQUAL_INT(INPUT_CSI_HANDLED,
                          input_parse_csi(&data, "<32;11;12M", 11, &consumed, &tracker));

    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 0.02F, camera.yaw);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, -0.04F, camera.pitch);
}

static void test_enhanced_keyboard_protocols_update_held_key_state(void) {
    KeyState key_state = {0};
    InputThreadData data = {.fps_controls = true, .key_state = &key_state};
    MouseTracker tracker = {0};
    size_t consumed = 0;

    TEST_ASSERT_EQUAL_INT(INPUT_CSI_INCOMPLETE,
                          input_parse_csi(&data, "119;1:", 6, &consumed, &tracker));
    static const char win32_press[] = "87;17;87;1;16;2_";
    input_parse_csi(&data, win32_press, sizeof(win32_press) - 1, &consumed, &tracker);
    TEST_ASSERT_TRUE(key_state.w);
    static const char win32_release[] = "87;17;87;0;16;1_";
    input_parse_csi(&data, win32_release, sizeof(win32_release) - 1, &consumed, &tracker);
    TEST_ASSERT_FALSE(key_state.w);

    static const char kitty_press[] = "119;1:1u";
    input_parse_csi(&data, kitty_press, sizeof(kitty_press) - 1, &consumed, &tracker);
    TEST_ASSERT_TRUE(key_state.w);
    static const char kitty_release[] = "119;1:3u";
    input_parse_csi(&data, kitty_release, sizeof(kitty_release) - 1, &consumed, &tracker);
    TEST_ASSERT_FALSE(key_state.w);

    Camera camera = {.position = {0.0F, 0.0F, 1.0F}};
    data.fps_controls = false;
    data.camera = &camera;
    static const char win32_orbit[] = "65;30;97;1;0;2_";
    input_parse_csi(&data, win32_orbit, sizeof(win32_orbit) - 1, &consumed, &tracker);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, ROTATION_AMOUNT, camera.yaw);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_character_cell_delta_uses_raw_sensitivity);
    RUN_TEST(test_enhanced_keyboard_protocols_update_held_key_state);
    return UNITY_END();
}
