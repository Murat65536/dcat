#ifndef DCAT_ARGS_H
#define DCAT_ARGS_H

#include <stdbool.h>

typedef struct Args {
    char* model_path;
    char* texture_path;
    char* normal_map_path;
    char* skydome_path;
    int width;
    int height;
    float camera_distance;
    float model_scale;
    int target_fps;
    bool no_lighting;
    bool fps_controls;
    bool show_status_bar;
    bool show_help;
    bool use_sixel;
    bool use_kitty;
} Args;

// Parse command line arguments
Args parse_args(int argc, char* argv[]);

// Print usage information
void print_usage(void);

// Validate parsed arguments
bool validate_args(const Args* args);

#endif
