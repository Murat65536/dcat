#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct OutputDriver {
    const char *name;
    bool uses_character_cells;
    void (*render_frame)(const uint8_t *framebuffer, uint32_t width, uint32_t height,
                         bool use_hash_characters);
} OutputDriver;
