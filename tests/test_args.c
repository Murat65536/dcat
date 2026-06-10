// Unit tests for src/core/args.c (parse_args + validate_args).
#include "core/args.h"
#include <string.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

#define ARGV_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Positional/string values alias argv storage, so the model argument must
// outlive the local argv array that parsed_model_only() builds.
static char g_model_arg[] = "model.obj";

static Args parsed_model_only(void) {
    Args args;
    char *argv[] = {"dcat", g_model_arg};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    return args;
}

static void test_defaults_and_reset(void) {
    Args args;
    memset(&args, 0xAA, sizeof args);

    char *argv[] = {"dcat"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));

    TEST_ASSERT_NULL(args.model_path);
    TEST_ASSERT_NULL(args.texture_path);
    TEST_ASSERT_NULL(args.normal_map_path);
    TEST_ASSERT_NULL(args.skydome_path);
    TEST_ASSERT_EQUAL_INT(-1, args.width);
    TEST_ASSERT_EQUAL_INT(-1, args.height);
    TEST_ASSERT_EQUAL_FLOAT(-1.0F, args.camera_distance);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, args.model_scale);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, args.spin_speed);
    TEST_ASSERT_EQUAL_FLOAT(0.02F, args.mouse_sensitivity);
    TEST_ASSERT_EQUAL_INT(60, args.target_fps);
    TEST_ASSERT_FALSE(args.no_lighting);
    TEST_ASSERT_FALSE(args.fps_controls);
    TEST_ASSERT_FALSE(args.mouse_orbit);
    TEST_ASSERT_FALSE(args.show_status_bar);
    TEST_ASSERT_FALSE(args.show_help);
    TEST_ASSERT_FALSE(args.use_sixel);
    TEST_ASSERT_FALSE(args.use_kitty);
    TEST_ASSERT_FALSE(args.use_kitty_shm);
    TEST_ASSERT_FALSE(args.use_truecolor_characters);
    TEST_ASSERT_FALSE(args.use_palette_characters);
    TEST_ASSERT_FALSE(args.use_block_characters);
    TEST_ASSERT_FALSE(args.use_hash_characters);
}

static void test_positional_model_path(void) {
    Args args;
    char *argv[] = {"dcat", "cat.obj"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_EQUAL_STRING("cat.obj", args.model_path);

    // A second positional overwrites the first.
    char *argv2[] = {"dcat", "first.obj", "second.obj"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv2), argv2, &args));
    TEST_ASSERT_EQUAL_STRING("second.obj", args.model_path);
}

static void test_string_options(void) {
    Args args;
    char *argv[] = {"dcat", "-t", "tex.png", "--normal-map", "n.png", "--skydome", "sky.png"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_EQUAL_STRING("tex.png", args.texture_path);
    TEST_ASSERT_EQUAL_STRING("n.png", args.normal_map_path);
    TEST_ASSERT_EQUAL_STRING("sky.png", args.skydome_path);
    // String values alias argv rather than copying it.
    TEST_ASSERT_EQUAL_PTR(argv[2], args.texture_path);

    char *argv2[] = {"dcat", "--texture", "long.png"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv2), argv2, &args));
    TEST_ASSERT_EQUAL_STRING("long.png", args.texture_path);
}

static void test_int_options(void) {
    Args args;
    char *argv[] = {"dcat", "-W", "120", "-H", "40", "-f", "30"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_EQUAL_INT(120, args.width);
    TEST_ASSERT_EQUAL_INT(40, args.height);
    TEST_ASSERT_EQUAL_INT(30, args.target_fps);

    char *argv2[] = {"dcat", "--width", "640", "--height", "480"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv2), argv2, &args));
    TEST_ASSERT_EQUAL_INT(640, args.width);
    TEST_ASSERT_EQUAL_INT(480, args.height);

    // The value slot accepts a leading '-'; range rejection is validate_args' job.
    char *argv3[] = {"dcat", "-W", "-5"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv3), argv3, &args));
    TEST_ASSERT_EQUAL_INT(-5, args.width);
}

static void test_float_options(void) {
    Args args;
    char *argv[] = {"dcat", "--camera-distance",   "5.5", "--model-scale", "0.5", "--spin",
                    "1.25", "--mouse-sensitivity", "0.1"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_EQUAL_FLOAT(5.5F, args.camera_distance);
    TEST_ASSERT_EQUAL_FLOAT(0.5F, args.model_scale);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, args.spin_speed);
    TEST_ASSERT_EQUAL_FLOAT(0.1F, args.mouse_sensitivity);

    char *argv2[] = {"dcat", "--spin", "1e-2"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv2), argv2, &args));
    TEST_ASSERT_EQUAL_FLOAT(0.01F, args.spin_speed);
}

static void test_flag_options(void) {
    Args args;
    char *argv[] = {"dcat", "--no-lighting",    "--keyboard-controls", "--mouse-orbit",
                    "-s",   "--hash-characters"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_TRUE(args.no_lighting);
    // --keyboard-controls intentionally maps to the fps_controls field.
    TEST_ASSERT_TRUE(args.fps_controls);
    TEST_ASSERT_TRUE(args.mouse_orbit);
    TEST_ASSERT_TRUE(args.show_status_bar);
    TEST_ASSERT_TRUE(args.use_hash_characters);
}

static void test_renderer_flag_mapping(void) {
    Args args;

    // -K/--kitty selects the shared-memory transport; --kitty-direct the inline one.
    char *argv[] = {"dcat", "-K"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv), argv, &args));
    TEST_ASSERT_TRUE(args.use_kitty_shm);
    TEST_ASSERT_FALSE(args.use_kitty);

    char *argv2[] = {"dcat", "--kitty"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv2), argv2, &args));
    TEST_ASSERT_TRUE(args.use_kitty_shm);
    TEST_ASSERT_FALSE(args.use_kitty);

    char *argv3[] = {"dcat", "--kitty-direct"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv3), argv3, &args));
    TEST_ASSERT_TRUE(args.use_kitty);
    TEST_ASSERT_FALSE(args.use_kitty_shm);

    char *argv4[] = {"dcat", "-S", "-T", "-P", "-B"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_OK, parse_args(ARGV_COUNT(argv4), argv4, &args));
    // The parser enforces no exclusivity between renderer flags; selection
    // precedence lives in driver_factory.
    TEST_ASSERT_TRUE(args.use_sixel);
    TEST_ASSERT_TRUE(args.use_truecolor_characters);
    TEST_ASSERT_TRUE(args.use_palette_characters);
    TEST_ASSERT_TRUE(args.use_block_characters);
}

static void test_help_requests(void) {
    Args args;
    char *argv[] = {"dcat", "-h"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_HELP, parse_args(ARGV_COUNT(argv), argv, &args));

    char *argv2[] = {"dcat", "--help"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_HELP, parse_args(ARGV_COUNT(argv2), argv2, &args));

    // Parsing continues past positionals before the final help check.
    char *argv3[] = {"dcat", "model.obj", "-h"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_HELP, parse_args(ARGV_COUNT(argv3), argv3, &args));
    TEST_ASSERT_EQUAL_STRING("model.obj", args.model_path);

    // An unknown option errors out before help is honored.
    char *argv4[] = {"dcat", "-h", "--bogus"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv4), argv4, &args));
}

static void test_error_unknown_option(void) {
    Args args;
    char *argv[] = {"dcat", "--bogus"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv), argv, &args));

    char *argv2[] = {"dcat", "-x"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv2), argv2, &args));

    char *argv3[] = {"dcat", "-"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv3), argv3, &args));
}

static void test_error_missing_value(void) {
    Args args;
    char *argv[] = {"dcat", "-W"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv), argv, &args));

    char *argv2[] = {"dcat", "--texture"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv2), argv2, &args));
}

static void test_error_bad_int(void) {
    Args args;
    char *argv[] = {"dcat", "-W", "abc"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv), argv, &args));

    char *argv2[] = {"dcat", "-W", "12x"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv2), argv2, &args));

    char *argv3[] = {"dcat", "-W", ""};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv3), argv3, &args));

    char *argv4[] = {"dcat", "-W", "99999999999999999999"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv4), argv4, &args));
}

static void test_error_bad_float(void) {
    Args args;
    char *argv[] = {"dcat", "--spin", "abc"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv), argv, &args));

    char *argv2[] = {"dcat", "--spin", "nan"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv2), argv2, &args));

    char *argv3[] = {"dcat", "--spin", "inf"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv3), argv3, &args));

    char *argv4[] = {"dcat", "--spin", "1e39"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv4), argv4, &args));

    char *argv5[] = {"dcat", "--spin", "1.5x"};
    TEST_ASSERT_EQUAL_INT(ARGS_PARSE_ERROR, parse_args(ARGV_COUNT(argv5), argv5, &args));
}

static void test_validate_accepts_model_defaults(void) {
    Args args = parsed_model_only();
    TEST_ASSERT_TRUE(validate_args(&args));
}

static void test_validate_rejects_missing_model(void) {
    Args args = parsed_model_only();
    args.model_path = NULL;
    TEST_ASSERT_FALSE(validate_args(&args));
}

static void test_validate_width_height_bounds(void) {
    Args args = parsed_model_only();

    args.width = -1; // sentinel: "not specified"
    TEST_ASSERT_TRUE(validate_args(&args));
    args.width = 0;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.width = -2;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.width = 1;
    TEST_ASSERT_TRUE(validate_args(&args));
    args.width = 65535;
    TEST_ASSERT_TRUE(validate_args(&args));
    args.width = 65536;
    TEST_ASSERT_FALSE(validate_args(&args));

    args = parsed_model_only();
    args.height = 0;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.height = 65535;
    TEST_ASSERT_TRUE(validate_args(&args));
    args.height = 65536;
    TEST_ASSERT_FALSE(validate_args(&args));
}

static void test_validate_fps_and_scale(void) {
    Args args = parsed_model_only();
    args.target_fps = 0;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.target_fps = -3;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.target_fps = 1;
    TEST_ASSERT_TRUE(validate_args(&args));

    args = parsed_model_only();
    args.model_scale = 0.0F;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.model_scale = -1.0F;
    TEST_ASSERT_FALSE(validate_args(&args));
    args.model_scale = 0.001F;
    TEST_ASSERT_TRUE(validate_args(&args));
}

static void test_validate_ignores_unchecked_fields(void) {
    // Documents current behavior: these fields have no validation.
    Args args = parsed_model_only();
    args.camera_distance = -99.0F;
    args.spin_speed = 1e30F;
    args.mouse_sensitivity = -1.0F;
    TEST_ASSERT_TRUE(validate_args(&args));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_and_reset);
    RUN_TEST(test_positional_model_path);
    RUN_TEST(test_string_options);
    RUN_TEST(test_int_options);
    RUN_TEST(test_float_options);
    RUN_TEST(test_flag_options);
    RUN_TEST(test_renderer_flag_mapping);
    RUN_TEST(test_help_requests);
    RUN_TEST(test_error_unknown_option);
    RUN_TEST(test_error_missing_value);
    RUN_TEST(test_error_bad_int);
    RUN_TEST(test_error_bad_float);
    RUN_TEST(test_validate_accepts_model_defaults);
    RUN_TEST(test_validate_rejects_missing_model);
    RUN_TEST(test_validate_width_height_bounds);
    RUN_TEST(test_validate_fps_and_scale);
    RUN_TEST(test_validate_ignores_unchecked_fields);
    return UNITY_END();
}
