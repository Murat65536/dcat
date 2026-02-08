#ifndef DCAT_TERMINAL_H
#define DCAT_TERMINAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 24

void get_terminal_size(uint32_t* cols, uint32_t* rows);
void get_terminal_size_pixels(uint32_t* width, uint32_t* height);
void calculate_render_dimensions(int explicit_width, int explicit_height,
                                 bool use_sixel, bool use_kitty,
                                 bool reserve_bottom_line,
                                 uint32_t* out_width, uint32_t* out_height);

void safe_write(const char *data, size_t size);

void get_terminal_size(uint32_t* cols, uint32_t* rows);

void get_terminal_size_pixels(uint32_t* width, uint32_t* height);

void draw_status_bar(float fps, float speed, const float* pos, const char* animation_name);

void enable_raw_mode(void);
void disable_raw_mode(void);
void enter_alternate_screen(void);
void exit_alternate_screen(void);
void hide_cursor(void);
void show_cursor(void);
void enable_focus_tracking(void);
void disable_focus_tracking(void);

#endif // DCAT_TERMINAL_H
