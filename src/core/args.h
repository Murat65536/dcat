#pragma once
#include <stdbool.h>

typedef struct Args {
    char *model_path;
    char *texture_path;
    char *normal_map_path;
    char *skydome_path;
    int width;
    int height;
    float camera_distance;
    float model_scale;
    float spin_speed;
    int target_fps;
    bool no_lighting;
    bool fps_controls;
    bool mouse_orbit;
    float mouse_sensitivity;
    bool show_status_bar;
    bool show_help;
    bool use_sixel;
    bool use_kitty;
    bool use_kitty_shm;
    bool use_truecolor_characters;
    bool use_palette_characters;
    bool use_block_characters;
    bool use_hash_characters;
} Args;

// Result of parsing the command line. The caller decides the process exit code,
// so parse_args never calls exit() itself.
typedef enum ArgsParseStatus {
    ARGS_PARSE_OK,    // out is fully populated; proceed
    ARGS_PARSE_HELP,  // -h/--help was requested; print usage and exit cleanly
    ARGS_PARSE_ERROR, // a diagnostic was already printed to stderr
} ArgsParseStatus;

// Parse command line arguments into *out (which is reset to defaults first).
ArgsParseStatus parse_args(int argc, char *argv[], Args *out);

// Print usage information
void print_usage(void);

// Validate parsed arguments
bool validate_args(const Args *args);
