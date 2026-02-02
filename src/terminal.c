#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sixel.h>

static struct termios original_termios;
static bool raw_mode_enabled = false;

// Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

// Helper to handle EINTR and partial writes
static void safe_write(const char* data, size_t size) {
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = write(STDOUT_FILENO, data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            break;
        }
        data += written;
        remaining -= written;
    }
}

// Fast buffer for terminal output
typedef struct {
    char* data;
    size_t capacity;
    char* ptr;
} FastBuffer;

static FastBuffer fast_buffer = {NULL, 0, NULL};

static void fast_buffer_ensure_capacity(size_t size) {
    if (fast_buffer.capacity < size) {
        free(fast_buffer.data);
        fast_buffer.capacity = size;
        fast_buffer.data = malloc(size);
    }
    fast_buffer.ptr = fast_buffer.data;
}

static void fast_buffer_append(const char* str, size_t len) {
    memcpy(fast_buffer.ptr, str, len);
    fast_buffer.ptr += len;
}

static void fast_buffer_append_u8(uint8_t v) {
    if (v >= 100) {
        *fast_buffer.ptr++ = '0' + v / 100;
        v %= 100;
        *fast_buffer.ptr++ = '0' + v / 10;
        *fast_buffer.ptr++ = '0' + v % 10;
    } else if (v >= 10) {
        *fast_buffer.ptr++ = '0' + v / 10;
        *fast_buffer.ptr++ = '0' + v % 10;
    } else {
        *fast_buffer.ptr++ = '0' + v;
    }
}

static void fast_buffer_append_color_block(uint8_t rU, uint8_t gU, uint8_t bU,
                                           uint8_t rL, uint8_t gL, uint8_t bL) {
    // Foreground color
    fast_buffer_append("\x1b[38;2;", 7);
    fast_buffer_append_u8(rU);
    *fast_buffer.ptr++ = ';';
    fast_buffer_append_u8(gU);
    *fast_buffer.ptr++ = ';';
    fast_buffer_append_u8(bU);
    
    // Background color
    fast_buffer_append(";48;2;", 6);
    fast_buffer_append_u8(rL);
    *fast_buffer.ptr++ = ';';
    fast_buffer_append_u8(gL);
    *fast_buffer.ptr++ = ';';
    fast_buffer_append_u8(bL);
    
    // Character (upper half block)
    fast_buffer_append("m\xE2\x96\x80", 4);
}

static void fast_buffer_flush(void) {
    safe_write(fast_buffer.data, fast_buffer.ptr - fast_buffer.data);
}

static char* base64_encode(const uint8_t* data, size_t length, size_t* out_len) {
    size_t result_len = ((length + 2) / 3) * 4;
    char* result = malloc(result_len + 1);
    if (!result) return NULL;
    
    char* p = result;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < length) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < length) n |= (uint32_t)data[i + 2];
        
        *p++ = base64_chars[(n >> 18) & 0x3F];
        *p++ = base64_chars[(n >> 12) & 0x3F];
        *p++ = (i + 1 < length) ? base64_chars[(n >> 6) & 0x3F] : '=';
        *p++ = (i + 2 < length) ? base64_chars[n & 0x3F] : '=';
    }
    *p = '\0';
    
    if (out_len) *out_len = result_len;
    return result;
}

void render_terminal(const uint8_t* buffer, uint32_t width, uint32_t height) {
    // Estimate buffer size
    size_t est_size = 100 + (width * ((height + 1) / 2) * 45);
    fast_buffer_ensure_capacity(est_size);
    
    // Header: Synchronized update start + Home cursor
    fast_buffer_append("\x1b[?2026h\x1b[H", 12);
    
    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t* row_upper = buffer + (y * width * 4);
        const uint8_t* row_lower = buffer + ((y + 1) * width * 4);
        bool has_lower_row = (y + 1 < height);
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t rU = row_upper[0], gU = row_upper[1], bU = row_upper[2];
            row_upper += 4;
            
            uint8_t rL = 0, gL = 0, bL = 0;
            if (has_lower_row) {
                rL = row_lower[0]; gL = row_lower[1]; bL = row_lower[2];
                row_lower += 4;
            }
            
            fast_buffer_append_color_block(rU, gU, bU, rL, gL, bL);
        }
    }
    
    // Footer: Clear formatting + Synchronized update end
    fast_buffer_append("\x1b[0m\x1b[?2026l", 13);
    
    fast_buffer_flush();
}

static int sixel_write(char* data, int size, void* priv) {
    (void)priv;
    safe_write(data, size);
    return size;
}

void render_sixel(const uint8_t* buffer, uint32_t width, uint32_t height) {
    safe_write("\x1b[H", 3);
    
    sixel_output_t* output = NULL;
    sixel_dither_t* dither = NULL;
    
    if (sixel_output_new(&output, sixel_write, NULL, NULL) != SIXEL_OK) {
        return;
    }
    
    if (sixel_dither_new(&dither, 256, NULL) != SIXEL_OK) {
        sixel_output_unref(output);
        return;
    }
    
    // Create a mutable copy since sixel_dither_initialize modifies the data
    size_t pixel_size = width * height * 4;
    uint8_t* pixels = malloc(pixel_size);
    if (!pixels) {
        sixel_dither_unref(dither);
        sixel_output_unref(output);
        return;
    }
    memcpy(pixels, buffer, pixel_size);
    
    sixel_dither_initialize(dither, pixels, width, height,
                            SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_NORM,
                            SIXEL_REP_CENTER_BOX, SIXEL_QUALITY_LOW);
    
    sixel_encode(pixels, width, height, 4, dither, output);
    
    free(pixels);
    sixel_dither_unref(dither);
    sixel_output_unref(output);
}

void render_kitty_shm(const uint8_t* buffer, uint32_t width, uint32_t height) {
    static uint32_t buffer_idx = 0;
    buffer_idx = (buffer_idx + 1) % 32;
    
    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/dcat_shm_%d_%u", getpid(), buffer_idx);
    
    size_t encoded_name_len;
    char* encoded_name = base64_encode((const uint8_t*)shm_name, strlen(shm_name), &encoded_name_len);
    if (!encoded_name) return;
    
    size_t data_size = width * height * 4;
    
    // Create shared memory fresh each frame (Kitty unlinks it after reading)
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        fprintf(stderr, "shm_open failed: %s\n", strerror(errno));
        free(encoded_name);
        return;
    }
    
    if (ftruncate(fd, data_size) == -1) {
        fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
        close(fd);
        shm_unlink(shm_name);
        free(encoded_name);
        return;
    }
    
    void* ptr = mmap(NULL, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd);
        shm_unlink(shm_name);
        free(encoded_name);
        return;
    }
    
    // Copy data to shared memory
    memcpy(ptr, buffer, data_size);
    
    // Unmap and close fd (Kitty will shm_unlink after reading)
    munmap(ptr, data_size);
    close(fd);
    
    // Send Kitty graphics command
    char cmd[512];
    int len = snprintf(cmd, sizeof(cmd), "\x1b_Ga=T,f=32,s=%u,v=%u,t=s,i=1,C=1,q=1;%s\x1b\\",
                       width, height, encoded_name);
    if (len > 0) {
        safe_write(cmd, (size_t)len);
    }
    
    free(encoded_name);
}

void get_terminal_size(uint32_t* cols, uint32_t* rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = DEFAULT_TERM_WIDTH;
        *rows = DEFAULT_TERM_HEIGHT;
    }
}

void get_terminal_size_pixels(uint32_t* width, uint32_t* height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        *width = ws.ws_xpixel;
        *height = ws.ws_ypixel;
    } else {
        *width = DEFAULT_TERM_WIDTH;
        *height = DEFAULT_TERM_HEIGHT;
    }
}

void draw_status_bar(float fps, float speed, const float* pos, const char* animation_name) {
    uint32_t cols, rows;
    get_terminal_size(&cols, &rows);
    if (rows == 0) return;
    
    char buffer[512];
    char anim_part[128] = "";
    if (animation_name && animation_name[0]) {
        snprintf(anim_part, sizeof(anim_part), " | ANIM: %s", animation_name);
    }
    
    int len = snprintf(buffer, sizeof(buffer),
        "\x1b[?2026h\x1b[%u;1H\x1b[2K\x1b[7m FPS: %.1f | SPEED: %.2f | POS: %.2f, %.2f, %.2f%s \x1b[0m\x1b[H\x1b[?2026l",
        rows, fps, speed, pos[0], pos[1], pos[2], anim_part);
    if (len > 0) {
        safe_write(buffer, (size_t)len);
    }
}

void calculate_render_dimensions(int explicit_width, int explicit_height,
                                  bool use_sixel, bool use_kitty,
                                  bool reserve_bottom_line,
                                  uint32_t* out_width, uint32_t* out_height) {
    if (explicit_width > 0 && explicit_height > 0) {
        *out_width = (uint32_t)explicit_width;
        *out_height = (uint32_t)explicit_height;
        return;
    }
    
    if (use_sixel || use_kitty) {
        uint32_t pixel_width, pixel_height;
        get_terminal_size_pixels(&pixel_width, &pixel_height);
        if (reserve_bottom_line) {
            uint32_t cols, rows;
            get_terminal_size(&cols, &rows);
            if (rows > 0) {
                uint32_t char_height = pixel_height / rows;
                if (pixel_height > char_height) {
                    pixel_height -= char_height;
                }
            }
        }
        *out_width = pixel_width;
        *out_height = pixel_height;
    } else {
        uint32_t cols, rows;
        get_terminal_size(&cols, &rows);
        if (reserve_bottom_line && rows > 0) {
            rows--;
        }
        *out_width = cols;
        *out_height = rows * 2;
    }
}

void enable_raw_mode(void) {
    if (raw_mode_enabled) return;
    
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled = true;
}

void disable_raw_mode(void) {
    if (!raw_mode_enabled) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    raw_mode_enabled = false;
}

void enter_alternate_screen(void) {
    safe_write("\x1b[?1049h", 8);
}

void exit_alternate_screen(void) {
    safe_write("\x1b[?1049l", 8);
}

void hide_cursor(void) {
    safe_write("\x1b[?25l", 6);
}

void show_cursor(void) {
    safe_write("\x1b[?25h", 6);
}

void enable_focus_tracking(void) {
    safe_write("\x1b[?1004h", 8);
}

void disable_focus_tracking(void) {
    safe_write("\x1b[?1004l", 8);
}
