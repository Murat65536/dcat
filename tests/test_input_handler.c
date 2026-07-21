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

    TEST_ASSERT_EQUAL_INT(MOUSE_CSI_HANDLED,
                          mouse_parse_csi(&data, "<0;10;10M", 10, &consumed, &tracker));
    TEST_ASSERT_EQUAL_INT(MOUSE_CSI_HANDLED,
                          mouse_parse_csi(&data, "<32;11;12M", 11, &consumed, &tracker));

    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 0.02F, camera.yaw);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, -0.04F, camera.pitch);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_character_cell_delta_uses_raw_sensitivity);
    return UNITY_END();
}
