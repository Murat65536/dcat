#pragma once
#include "platform/io.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ChafaTermInfo;
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <termios.h>
#endif

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 24
#define SYMBOL_CELL_SOURCE_WIDTH 2U
#define SYMBOL_CELL_SOURCE_HEIGHT 4U

void get_terminal_size(uint32_t *cols, uint32_t *rows);
void get_terminal_size_pixels(uint32_t *width, uint32_t *height);
void calculate_render_dimensions(int explicit_width, int explicit_height, bool use_pixel_protocol,
                                 bool use_hash_characters, bool reserve_bottom_line,
                                 uint32_t *out_width, uint32_t *out_height);

void safe_write(const char *data, size_t size);

void draw_status_bar(float fps, float speed, const float *pos, const char *animation_name);

typedef struct {
#ifdef _WIN32
    HANDLE handle;
    DWORD saved_mode;
    DWORD mode;
    HANDLE output_handle;
    DWORD saved_output_mode;
    bool output_valid;
    bool valid;
#else
    int fd;
    struct termios saved;
    struct termios settings;
#endif
} TermiosState;

bool termios_state_init(TermiosState *state, int fd);
bool termios_state_apply(TermiosState *state);
void termios_state_restore(TermiosState *state);
bool terminal_begin_query_mode(TermiosState *state);
void terminal_end_query_mode(TermiosState *state);

void enable_raw_mode(void);
void disable_raw_mode(void);
void terminal_set_mouse_input_enabled(bool enabled);
void terminal_arm_recovery(void);
void terminal_disarm_recovery(void);
void write_terminal_recovery_sequence(int fd);
void terminal_restore_default_state(void);
void terminal_restore_after_crash(void);

ssize_t terminal_read_query(char *buffer, size_t size, char terminator);
bool terminal_parse_sequence(struct ChafaTermInfo *term_info, int sequence, const char *response,
                             size_t response_length, unsigned int *args_out, int *n_args_out);
int terminal_base64_encode(const char *src, int len, char *dst);
