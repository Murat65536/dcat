#include "terminal/palette_characters.h"
#include "terminal/terminal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

// Persistent buffer with fixed structure - only palette indices change
static char *render_buf = NULL;
static size_t render_buf_size = 0;
static uint32_t last_width = 0;
static uint32_t last_height = 0;
static bool buffer_initialized = false;

// Lookup table: uint8 -> 3-digit ASCII string with leading zeros
static char u8_3digit[256][3];

__attribute__((constructor))
static void init_u8_table(void) {
    for (int i = 0; i < 256; i++) {
        u8_3digit[i][0] = '0' + (i / 100);
        u8_3digit[i][1] = '0' + ((i / 10) % 10);
        u8_3digit[i][2] = '0' + (i % 10);
    }
}

static inline uint8_t rgb_to_256(uint8_t r, uint8_t g, uint8_t b) {
    // Check if grayscale
    if (r == g && g == b) {
        if (r < 8) return 16;
        if (r > 248) return 231;
        return 232 + (r - 8) / 10;
    }

    // 6x6x6 color cube
    int vr = (r <= 47) ? 0 : (r - 55) / 40 + 1;
    int vg = (g <= 47) ? 0 : (g - 55) / 40 + 1;
    int vb = (b <= 47) ? 0 : (b - 55) / 40 + 1;
    
    if (vr > 5) vr = 5;
    if (vg > 5) vg = 5;
    if (vb > 5) vb = 5;
    
    return 16 + 36 * vr + 6 * vg + vb;
}

void render_palette_characters(const uint8_t *buffer, uint32_t width, uint32_t height) {
    uint32_t num_blocks = width * ((height + 1) / 2);

    if (!buffer_initialized || width != last_width || height != last_height) {
        size_t needed_size = 12 + num_blocks * 23 + 13;
        
        if (render_buf_size < needed_size) {
            free(render_buf);
            render_buf = (char *)malloc(needed_size);
            render_buf_size = needed_size;
        }
        
        char *p = render_buf;
        
        // Header
        memcpy(p, "\x1b[?2026h\x1b[H", 12);
        p += 12;
        
        for (uint32_t i = 0; i < num_blocks; i++) {
            memcpy(p, "\x1b[38;5;000;48;5;000m\xe2\x96\x80", 23);
            p += 23;
        }
        
        // Footer
        memcpy(p, "\x1b[0m\x1b[?2026l", 13);
        
        last_width = width;
        last_height = height;
        buffer_initialized = true;
    }
    
    char *block_ptr = render_buf + 12;
    
    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t *row_upper = buffer + (y * width * 4);
        const uint8_t *row_lower = buffer + ((y + 1) * width * 4);
        bool has_lower = (y + 1 < height);
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t rU = row_upper[0], gU = row_upper[1], bU = row_upper[2];
            row_upper += 4;
            
            uint8_t idxU = rgb_to_256(rU, gU, bU);
            
            uint8_t idxL = 0;
            if (has_lower) {
                uint8_t rL = row_lower[0], gL = row_lower[1], bL = row_lower[2];
                row_lower += 4;
                idxL = rgb_to_256(rL, gL, bL);
            }

            memcpy(block_ptr + 7, u8_3digit[idxU], 3);
            memcpy(block_ptr + 16, u8_3digit[idxL], 3);
            
            block_ptr += 23;
        }
    }
    
    safe_write(render_buf, 12 + num_blocks * 23 + 13);
}
