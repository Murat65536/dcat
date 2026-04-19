#ifndef DCAT_TERMINAL_H
#define DCAT_TERMINAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
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

void get_terminal_size(uint32_t* cols, uint32_t* rows);
void get_terminal_size_pixels(uint32_t* width, uint32_t* height);
void calculate_render_dimensions(int explicit_width, int explicit_height,
                                 bool use_sixel, bool use_kitty,
                                 bool reserve_bottom_line,
                                 uint32_t* out_width, uint32_t* out_height);

void safe_write(const char *data, size_t size);

void draw_status_bar(float fps, float speed, const float* pos, const char* animation_name);

typedef struct {
#ifdef _WIN32
    HANDLE handle;
    DWORD saved_mode;
    DWORD mode;
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
void terminal_arm_recovery(void);
void terminal_disarm_recovery(void);
void write_terminal_recovery_sequence(int fd);
void terminal_restore_default_state(void);
void terminal_restore_after_crash(void);

static inline void enter_alternate_screen(void) { safe_write("\x1b[?1049h", 8); }
static inline void exit_alternate_screen(void) { safe_write("\x1b[?1049l", 8); }
static inline void hide_cursor(void) { safe_write("\x1b[?25l", 6); }
static inline void show_cursor(void) { safe_write("\x1b[?25h", 6); }
static inline void enable_mouse_orbit_tracking(void) { safe_write("\x1b[?1002h\x1b[?1006h\x1b[?1016h", 24); }
static inline void disable_mouse_orbit_tracking(void) { safe_write("\x1b[?1016l\x1b[?1006l\x1b[?1005l\x1b[?1004l\x1b[?1003l\x1b[?1002l\x1b[?1000l", 56); }
static inline void enable_kitty_keyboard(void) { safe_write("\x1b[>31u", 6); }
static inline void disable_kitty_keyboard(void) { safe_write("\x1b[<u", 4); }

#endif // DCAT_TERMINAL_H
