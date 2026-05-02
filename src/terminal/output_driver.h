#ifndef DCAT_OUTPUT_DRIVER_H
#define DCAT_OUTPUT_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct OutputDriver {
    const char *name;
    const char *flag_name;
    bool uses_kitty_protocol;
    bool uses_character_cells;
    bool (*detect_support)(void);
    void (*render_frame)(const uint8_t *framebuffer, uint32_t width, uint32_t height, bool use_hash_characters);
} OutputDriver;

#endif // DCAT_OUTPUT_DRIVER_H
