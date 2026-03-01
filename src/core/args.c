#include "args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(void) {
    printf("Usage: dcat [OPTION]... [MODEL]\n\n");
    printf("  -t, --texture PATH         path to the texture file\n");
    printf("  -n, --normal-map PATH      path to normal image file\n");
    printf("      --skydome PATH         path to skydome texture file\n");
    printf("  -W, --width WIDTH          renderer width\n");
    printf("  -H, --height HEIGHT        renderer height\n");
    printf("      --camera-distance DIST camera distance from origin\n");
    printf("      --model-scale SCALE    scale multiplier for the model\n");
    printf("      --spin SPEED           spin the model at specified speed (rad/s)\n");
    printf("  -f, --fps FPS              target frames per second\n");
    printf("      --no-lighting          disable lighting calculations\n");
    printf("      --keyboard-controls    enable first-person camera controls\n");
    printf("  -s, --status-bar           show status bar\n");
    printf("  -S, --sixel                enable Sixel graphics mode\n");
    printf("  -K, --kitty                enable Kitty graphics protocol mode\n");
    printf("  -T, --terminal-pixels      enable terminal pixels mode\n");
    printf("  -h, --help                 display this help and exit\n\n");
}

Args parse_args(int argc, char* argv[]) {
    Args args = {0};
    args.width = -1;
    args.height = -1;
    args.camera_distance = -1.0f;
    args.model_scale = 1.0f;
    args.target_fps = 60;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--texture") == 0) {
            if (++i < argc) args.texture_path = argv[i];
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--normal-map") == 0) {
            if (++i < argc) args.normal_map_path = argv[i];
        } else if (strcmp(argv[i], "--skydome") == 0) {
            if (++i < argc) args.skydome_path = argv[i];
        } else if (strcmp(argv[i], "-W") == 0 || strcmp(argv[i], "--width") == 0) {
            if (++i < argc) args.width = atoi(argv[i]);
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--height") == 0) {
            if (++i < argc) args.height = atoi(argv[i]);
        } else if (strcmp(argv[i], "--camera-distance") == 0) {
            if (++i < argc) args.camera_distance = atof(argv[i]);
        } else if (strcmp(argv[i], "--model-scale") == 0) {
            if (++i < argc) args.model_scale = atof(argv[i]);
        } else if (strcmp(argv[i], "--spin") == 0) {
            if (++i < argc) args.spin_speed = atof(argv[i]);
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) {
            if (++i < argc) args.target_fps = atoi(argv[i]);
        } else if (strcmp(argv[i], "--no-lighting") == 0) {
            args.no_lighting = true;
        } else if (strcmp(argv[i], "--keyboard-controls") == 0) {
            args.fps_controls = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status-bar") == 0) {
            args.show_status_bar = true;
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--sixel") == 0) {
            args.use_sixel = true;
        } else if (strcmp(argv[i], "-K") == 0 || strcmp(argv[i], "--kitty") == 0) {
            args.use_kitty = true;
        } else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--terminal-pixels") == 0) {
            args.use_terminal_pixels = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args.show_help = true;
        } else if (argv[i][0] != '-') {
            args.model_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            exit(1);
        }
    }
    
    if (args.show_help) {
        print_usage();
        exit(0);
    }
    
    return args;
}

bool validate_args(const Args* args) {
    if (!args->model_path) {
        fprintf(stderr, "Error: No model file specified\n");
        print_usage();
        return false;
    }
    
    if (args->width > 0 && (args->width <= 0 || args->width > 65535)) {
        fprintf(stderr, "Invalid width: %d (must be 1-65535)\n", args->width);
        return false;
    }
    
    if (args->height > 0 && (args->height <= 0 || args->height > 65535)) {
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
    
    return true;
}
