#include "args.h"
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(void) {
    printf("Usage: dcat [OPTION]... [MODEL]\n\n"
           "  -t, --texture PATH         path to the texture file\n"
           "  -n, --normal-map PATH      path to normal image file\n"
           "      --skydome PATH         path to skydome texture file\n"
           "  -W, --width WIDTH          renderer width\n"
           "  -H, --height HEIGHT        renderer height\n"
           "      --camera-distance DIST camera distance from origin\n"
           "      --model-scale SCALE    scale multiplier for the model\n"
           "      --spin SPEED           spin the model at specified speed (rad/s)\n"
           "  -f, --fps FPS              target frames per second\n"
           "      --no-lighting          disable lighting calculations\n"
           "      --keyboard-controls    enable first-person camera controls\n"
           "      --mouse-orbit          enable mouse drag to orbit the model\n"
           "      --mouse-sensitivity S  mouse drag sensitivity\n"
           "  -s, --status-bar           show status bar\n"
           "  -S, --sixel                enable Sixel graphics mode\n"
           "  -K, --kitty                enable SHM Kitty graphics protocol\n"
           "      --kitty-direct         enable inline Kitty graphics protocol\n"
           "  -T, --truecolor-characters enable truecolor characters mode\n"
           "  -P, --palette-characters   enable palette characters mode\n"
           "  -B, --block-characters     enable monochrome block characters mode\n"
           "      --hash-characters      use # for character modes\n"
           "  -h, --help                 display help\n"
           "      --controls             display controls\n");
}

void print_controls(void) {
    printf("Controls:\n"
           "q - Quit\n"
           "m - Toggle wireframe\n"
           "Animation:\n"
           "p - Play/pause animation\n"
           "1 - Previous animation\n"
           "2 - Next animation\n\n"
           "FPS controls:\n"
           "wasd - Move\n"
           "ijkl - Look around\n"
           "space - Move up\n"
           "shift - Move down\n"
           "v - slow down\n"
           "b - speed up\n"
           "Regular keyboard controls:\n"
           "wasd - rotate around the model\n"
           "e - zoom in\n"
           "r - zoom out\n");
}

typedef enum OptType {
    OPT_FLAG,
    OPT_STRING,
    OPT_INT,
    OPT_FLOAT,
} OptType;

typedef struct OptionSpec {
    const char *short_name;
    const char *long_name;
    OptType type;
    size_t offset;
} OptionSpec;

static const OptionSpec OPTIONS[] = {
    {"-t", "--texture", OPT_STRING, offsetof(Args, texture_path)},
    {"-n", "--normal-map", OPT_STRING, offsetof(Args, normal_map_path)},
    {NULL, "--skydome", OPT_STRING, offsetof(Args, skydome_path)},
    {"-W", "--width", OPT_INT, offsetof(Args, width)},
    {"-H", "--height", OPT_INT, offsetof(Args, height)},
    {NULL, "--camera-distance", OPT_FLOAT, offsetof(Args, camera_distance)},
    {NULL, "--model-scale", OPT_FLOAT, offsetof(Args, model_scale)},
    {NULL, "--spin", OPT_FLOAT, offsetof(Args, spin_speed)},
    {"-f", "--fps", OPT_INT, offsetof(Args, target_fps)},
    {NULL, "--no-lighting", OPT_FLAG, offsetof(Args, no_lighting)},
    {NULL, "--keyboard-controls", OPT_FLAG, offsetof(Args, fps_controls)},
    {NULL, "--mouse-orbit", OPT_FLAG, offsetof(Args, mouse_orbit)},
    {NULL, "--mouse-sensitivity", OPT_FLOAT, offsetof(Args, mouse_sensitivity)},
    {"-s", "--status-bar", OPT_FLAG, offsetof(Args, show_status_bar)},
    {"-S", "--sixel", OPT_FLAG, offsetof(Args, use_sixel)},
    {"-K", "--kitty", OPT_FLAG, offsetof(Args, use_kitty_shm)},
    {NULL, "--kitty-direct", OPT_FLAG, offsetof(Args, use_kitty)},
    {"-T", "--truecolor-characters", OPT_FLAG, offsetof(Args, use_truecolor_characters)},
    {"-P", "--palette-characters", OPT_FLAG, offsetof(Args, use_palette_characters)},
    {"-B", "--block-characters", OPT_FLAG, offsetof(Args, use_block_characters)},
    {NULL, "--hash-characters", OPT_FLAG, offsetof(Args, use_hash_characters)},
    {"-h", "--help", OPT_FLAG, offsetof(Args, show_help)},
    {NULL, "--controls", OPT_FLAG, offsetof(Args, show_controls)}};

static const OptionSpec *find_option(const char *arg) {
    for (size_t i = 0; i < sizeof(OPTIONS) / sizeof(OPTIONS[0]); i++) {
        const OptionSpec *spec = &OPTIONS[i];
        if ((spec->short_name != NULL && strcmp(arg, spec->short_name) == 0) ||
            strcmp(arg, spec->long_name) == 0) {
            return spec;
        }
    }
    return NULL;
}

static char *next_option_value(const char *option, const int argc, char *argv[], int *index) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", option);
        print_usage();
        return NULL;
    }

    (*index)++;
    return argv[*index];
}

static bool parse_int_arg(const char *option, const char *value, int *out) {
    char *end = NULL;
    long parsed = 0;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        fprintf(stderr, "Invalid integer for %s: %s\n", option, value);
        return false;
    }

    *out = (int)parsed;
    return true;
}

static bool parse_float_arg(const char *option, const char *value, float *out) {
    char *end = NULL;

    errno = 0;
    double parsed = strtod(value, &end);
    if (errno == ERANGE || end == value || *end != '\0' || !isfinite(parsed) || parsed < -FLT_MAX ||
        parsed > FLT_MAX) {
        fprintf(stderr, "Invalid number for %s: %s\n", option, value);
        return false;
    }

    *out = (float)parsed;
    return true;
}

// Store a parsed value into the Args member identified by spec->offset. The
// offset is from offsetof, so the destination is correctly aligned for its type.
static bool store_option_value(const OptionSpec *spec, const char *arg, char *value, Args *out) {
    char *field = (char *)out + spec->offset;
    switch (spec->type) {
    case OPT_STRING:
        *(char **)field = value;
        return true;
    case OPT_INT:
        return parse_int_arg(arg, value, (int *)field);
    case OPT_FLOAT:
        return parse_float_arg(arg, value, (float *)field);
    case OPT_FLAG:
        return true; // flags carry no value; handled by the caller
    }
    return false;
}

ArgsParseStatus parse_args(const int argc, char *argv[], Args *out) {
    *out = (Args){0};
    out->width = -1;
    out->height = -1;
    out->camera_distance = -1.0F;
    out->model_scale = 1.0F;
    out->mouse_sensitivity = 0.02F;
    out->target_fps = 60;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (arg[0] != '-') {
            out->model_path = arg;
            continue;
        }

        const OptionSpec *spec = find_option(arg);
        if (spec == NULL) {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage();
            return ARGS_PARSE_ERROR;
        }

        if (spec->type == OPT_FLAG) {
            *(bool *)((char *)out + spec->offset) = true;
            continue;
        }

        char *value = next_option_value(arg, argc, argv, &i);
        if (value == NULL || !store_option_value(spec, arg, value, out)) {
            return ARGS_PARSE_ERROR;
        }
    }

    if (out->show_help) {
        return ARGS_PARSE_HELP;
    }

    if (out->show_controls) {
        return ARGS_PARSE_CONTROLS;
    }

    return ARGS_PARSE_OK;
}

bool validate_args(const Args *args) {
    if (!args->model_path) {
        fprintf(stderr, "Error: No model file specified\n");
        print_usage();
        return false;
    }

    if (args->width != -1 && (args->width <= 0 || args->width > 65535)) {
        fprintf(stderr, "Invalid width: %d (must be 1-65535)\n", args->width);
        return false;
    }

    if (args->height != -1 && (args->height <= 0 || args->height > 65535)) {
        fprintf(stderr, "Invalid height: %d (must be 1-65535)\n", args->height);
        return false;
    }

    if (args->target_fps <= 0) {
        fprintf(stderr, "Invalid FPS: %d (must be greater than 0)\n", args->target_fps);
        return false;
    }

    if (args->model_scale <= 0) {
        fprintf(stderr, "Invalid scale: %f (must be greater than 0)\n", args->model_scale);
        return false;
    }

    // camera_distance keeps its -1 sentinel (auto); any other value must be positive
    // because driver-side setup only honours distances greater than zero.
    if (args->camera_distance != -1.0F && args->camera_distance <= 0) {
        fprintf(stderr, "Invalid camera distance: %f (must be greater than 0)\n",
                args->camera_distance);
        return false;
    }

    if (args->mouse_sensitivity <= 0) {
        fprintf(stderr, "Invalid mouse sensitivity: %f (must be greater than 0)\n",
                args->mouse_sensitivity);
        return false;
    }

    // The six render-mode flags each select one output driver; enabling more than one
    // leaves the choice to driver_factory precedence, which is almost certainly not
    // what the user meant.
    const int render_modes = (int)args->use_sixel + (int)args->use_kitty +
                             (int)args->use_kitty_shm + (int)args->use_truecolor_characters +
                             (int)args->use_palette_characters + (int)args->use_block_characters;
    if (render_modes > 1) {
        fprintf(stderr, "Conflicting render modes: choose at most one of --sixel, --kitty, "
                        "--kitty-direct, --truecolor-characters, --palette-characters, "
                        "--block-characters\n");
        return false;
    }

    return true;
}
