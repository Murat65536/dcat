#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ChafaTermInfo;

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 24
#define SYMBOL_CELL_SOURCE_WIDTH 2U
#define SYMBOL_CELL_SOURCE_HEIGHT 4U

void get_terminal_size(uint32_t *cols, uint32_t *rows);
void get_terminal_size_pixels(uint32_t *width, uint32_t *height);
void calculate_render_dimensions(int explicit_width, int explicit_height, bool use_pixel_protocol,
                                 bool reserve_bottom_line, uint32_t *out_width,
                                 uint32_t *out_height);

void safe_write(const char *data, size_t size);
void terminal_frame_begin(void);
void terminal_frame_end(void);
void terminal_frame_writer_shutdown(void);

void draw_status_bar(float fps, float speed, const float *pos, const char *animation_name);

void enable_raw_mode(void);
void terminal_set_mouse_input_enabled(bool enabled);
void terminal_arm_recovery(struct ChafaTermInfo *term_info);
void terminal_restore_default_state(void);

bool terminal_parse_sequence(struct ChafaTermInfo *term_info, int sequence, const char *response,
                             size_t response_length, unsigned int *args_out, int *n_args_out);
