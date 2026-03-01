#include "terminal/truecolor_characters.h"
#include "terminal/terminal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

// Persistent buffer with fixed structure - only RGB digits change
static char *render_buf = NULL;
static size_t render_buf_size = 0;
static uint32_t last_width = 0;
static uint32_t last_height = 0;
static bool buffer_initialized = false;

// Lookup table: uint8 -> 3-digit ASCII string (fixed width)
static char u8_3digit[256][3];

__attribute__((constructor))
static void init_u8_table(void) {
    for (int i = 0; i < 256; i++) {
        u8_3digit[i][0] = '0' + (i / 100);
        u8_3digit[i][1] = '0' + ((i / 10) % 10);
        u8_3digit[i][2] = '0' + (i % 10);
    }
}

static inline void write_u8_3digit(char *p, uint8_t v) {
    memcpy(p, u8_3digit[v], 3);
}

void render_truecolor_characters(const uint8_t *buffer, uint32_t width, uint32_t height) {
    uint32_t num_blocks = width * ((height + 1) / 2);
    
    // Detect resize or first run
    if (!buffer_initialized || width != last_width || height != last_height) {
        // Each block: \x1b[38;2;RRR;GGG;BBB;48;2;RRR;GGG;BBBm▀ = 39 bytes
        // Header: \x1b[?2026h\x1b[H = 12 bytes
        // Footer: \x1b[0m\x1b[?2026l = 13 bytes
        size_t needed_size = 12 + num_blocks * 39 + 13;
        
        if (render_buf_size < needed_size) {
            free(render_buf);
            render_buf = (char *)malloc(needed_size);
            render_buf_size = needed_size;
        }
        
        // Build the fixed structure
        char *p = render_buf;
        
        // Header
        memcpy(p, "\x1b[?2026h\x1b[H", 12);
        p += 12;
        
        // Build each block with placeholder RGB values (000)
        for (uint32_t i = 0; i < num_blocks; i++) {
            memcpy(p, "\x1b[38;2;000;000;000;48;2;000;000;000m\xe2\x96\x80", 39);
            p += 39;
        }
        
        // Footer
        memcpy(p, "\x1b[0m\x1b[?2026l", 13);
        
        last_width = width;
        last_height = height;
        buffer_initialized = true;
    }
    
    // Fast path: only update RGB digits in-place
    char *block_ptr = render_buf + 12;  // Skip header
    
    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t *row_upper = buffer + (y * width * 4);
        const uint8_t *row_lower = buffer + ((y + 1) * width * 4);
        bool has_lower = (y + 1 < height);
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t rU = row_upper[0], gU = row_upper[1], bU = row_upper[2];
            row_upper += 4;
            
            uint8_t rL = 0, gL = 0, bL = 0;
            if (has_lower) {
                rL = row_lower[0];
                gL = row_lower[1];
                bL = row_lower[2];
                row_lower += 4;
            }
            
            // Update foreground RGB: positions 7, 11, 15 within block
            write_u8_3digit(block_ptr + 7, rU);
            write_u8_3digit(block_ptr + 11, gU);
            write_u8_3digit(block_ptr + 15, bU);
            
            // Update background RGB: positions 24, 28, 32 within block
            write_u8_3digit(block_ptr + 24, rL);
            write_u8_3digit(block_ptr + 28, gL);
            write_u8_3digit(block_ptr + 32, bL);
            
            block_ptr += 39;
        }
    }
    
    safe_write(render_buf, 12 + num_blocks * 39 + 13);
}

bool detect_truecolor_support(void) {
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        return true;
    }

    const char *term = getenv("TERM");
    if (term && (strstr(term, "iterm") || strstr(term, "konsole") || strstr(term, "st-256color"))) {
        return true;
    }

    // Fallback: Query terminal for RGB capability: DCS + q 524742 ST
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

    TermiosState ts;
    if (!termios_state_init(&ts, STDIN_FILENO))
        return false;
    ts.settings.c_lflag &= ~(ICANON | ECHO);
    ts.settings.c_cc[VMIN] = 0;
    ts.settings.c_cc[VTIME] = 1; // 100ms timeout
    if (!termios_state_apply(&ts))
        return false;

    // Query RGB capability (524742 is hex for "RGB")
    safe_write("\x1bP+q524742\x1b\\", 11);

    char response[128];
    bool found = false;
    ssize_t r = read(STDIN_FILENO, response, sizeof(response) - 1);
    if (r > 0) {
        response[r] = '\0';
        // Success response: DCS 1 + r 524742=... ST
        if (strstr(response, "1+r524742")) {
            found = true;
        }
    }

    termios_state_restore(&ts);
    return found;
}
