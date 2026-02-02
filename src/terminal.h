#ifndef DCAT_TERMINAL_H
#define DCAT_TERMINAL_H

#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 48

// Render framebuffer to terminal using half-block characters
void render_terminal(const uint8_t* buffer, uint32_t width, uint32_t height);

// Render framebuffer using Sixel graphics
void render_sixel(const uint8_t* buffer, uint32_t width, uint32_t height);

// Render framebuffer using Kitty graphics protocol with shared memory
void render_kitty_shm(const uint8_t* buffer, uint32_t width, uint32_t height);

// Calculate render dimensions based on terminal size and mode
void calculate_render_dimensions(int explicit_width, int explicit_height,
                                  bool use_sixel, bool use_kitty,
                                  bool reserve_bottom_line,
                                  uint32_t* out_width, uint32_t* out_height);

// Get terminal size in characters
void get_terminal_size(uint32_t* cols, uint32_t* rows);

// Get terminal size in pixels
void get_terminal_size_pixels(uint32_t* width, uint32_t* height);

// Draw status bar at bottom of terminal
void draw_status_bar(float fps, float speed, const float* pos, const char* animation_name);

// Terminal mode functions
void enable_raw_mode(void);
void disable_raw_mode(void);
void enter_alternate_screen(void);
void exit_alternate_screen(void);
void hide_cursor(void);
void show_cursor(void);
void enable_focus_tracking(void);
void disable_focus_tracking(void);

#endif // DCAT_TERMINAL_H
