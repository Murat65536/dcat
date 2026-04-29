#include "terminal.h"
#include "core/platform_compat.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_MOUSE_INPUT
#define ENABLE_MOUSE_INPUT 0x0010
#endif
#endif

static const char TERMINAL_RECOVERY_SEQUENCE[] =
#ifndef _WIN32
    "\x1b[?2026l"
#endif
    "\x1b[<u"
    "\x1b[?1000l"
    "\x1b[?1002l"
    "\x1b[?1003l"
    "\x1b[?1004l"
    "\x1b[?1005l"
    "\x1b[?1006l"
    "\x1b[?1015l"
    "\x1b[?1016l"
    "\x1b[0m"
    "\x1b[?25h"
    "\x1b[?1049l";

#ifndef _WIN32
static bool get_winsize(struct winsize *ws) {
    return ioctl(STDOUT_FILENO, TIOCGWINSZ, ws) == 0;
}
#endif

void get_terminal_size(uint32_t *cols, uint32_t *rows) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE && out != NULL && GetConsoleScreenBufferInfo(out, &info)) {
        *cols = (uint32_t)(info.srWindow.Right - info.srWindow.Left + 1);
        *rows = (uint32_t)(info.srWindow.Bottom - info.srWindow.Top + 1);
    } else {
        *cols = DEFAULT_TERM_WIDTH;
        *rows = DEFAULT_TERM_HEIGHT;
    }
#else
    struct winsize ws;
    if (get_winsize(&ws)) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = DEFAULT_TERM_WIDTH;
        *rows = DEFAULT_TERM_HEIGHT;
    }
#endif
}

void get_terminal_size_pixels(uint32_t *width, uint32_t *height) {
#ifdef _WIN32
    static uint32_t cached_cols = 0, cached_rows = 0;
    static uint32_t cached_pixel_width = 0, cached_pixel_height = 0;

    uint32_t cols, rows;
    get_terminal_size(&cols, &rows);

    if (cols == cached_cols && rows == cached_rows && cached_pixel_width > 0) {
        *width = cached_pixel_width;
        *height = cached_pixel_height;
        return;
    }

    if (isatty(STDOUT_FILENO) && isatty(STDIN_FILENO)) {
        TermiosState ts;
        if (terminal_begin_query_mode(&ts)) {
            safe_write("\x1b[14t", 5);
            char buf[64];
            ssize_t r = terminal_read_query(buf, sizeof(buf) - 1, 't');
            terminal_end_query_mode(&ts);
            if (r > 0) {
                buf[r] = '\0';
                char *p = strstr(buf, "\x1b[4;");
                unsigned int h = 0, w = 0;
                if (p && sscanf(p, "\x1b[4;%u;%ut", &h, &w) == 2 && h > 0 && w > 0) {
                    *width = w;
                    *height = h;
                    cached_cols = cols;
                    cached_rows = rows;
                    cached_pixel_width = w;
                    cached_pixel_height = h;
                    return;
                }
            }
        }
    }

    *width = cols > 0 ? cols * 10 : DEFAULT_TERM_WIDTH * 10;
    *height = rows > 0 ? rows * 20 : DEFAULT_TERM_HEIGHT * 20;
    cached_cols = cols;
    cached_rows = rows;
    cached_pixel_width = *width;
    cached_pixel_height = *height;
#else
    struct winsize ws;
    if (get_winsize(&ws) && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        *width = ws.ws_xpixel;
        *height = ws.ws_ypixel;
    } else {
        *width = DEFAULT_TERM_WIDTH;
        *height = DEFAULT_TERM_HEIGHT;
    }
#endif
}

void calculate_render_dimensions(int explicit_width, int explicit_height, bool use_sixel,
                                 bool use_kitty, bool use_hash_characters, bool reserve_bottom_line,
                                 uint32_t *out_width, uint32_t *out_height) {
    if (explicit_width > 0 && explicit_height > 0) {
        *out_width = (uint32_t)explicit_width;
        *out_height = (uint32_t)explicit_height;
        return;
    }

    if (use_sixel || use_kitty) {
        get_terminal_size_pixels(out_width, out_height);
        if (reserve_bottom_line) {
            uint32_t cols, rows;
            get_terminal_size(&cols, &rows);
            if (rows > 0) {
                const uint32_t char_height = *out_height / rows;
                if (*out_height > char_height) {
                    *out_height -= char_height;
                }
            }
        }
        return;
    }

    uint32_t cols, rows;
    get_terminal_size(&cols, &rows);
    if (reserve_bottom_line && rows > 0) {
        rows--;
    }
    *out_width = cols;
    *out_height = use_hash_characters ? rows : rows * 2;
}

static TermiosState raw_mode_state;
static bool raw_mode_enabled = false;
static bool terminal_recovery_registered = false;
static volatile sig_atomic_t terminal_recovery_armed = 0;
static int terminal_recovery_fd = STDOUT_FILENO;
static bool terminal_sanitizer_callback_installed = false;
#ifdef _WIN32
static HANDLE raw_mode_output_handle = INVALID_HANDLE_VALUE;
static DWORD raw_mode_output_saved = 0;
static bool raw_mode_output_saved_valid = false;
static UINT raw_mode_input_code_page = 0;
static UINT raw_mode_output_code_page = 0;
static bool raw_mode_code_pages_saved = false;
#endif
static void terminal_write_fd(int fd, const char *data, size_t size);

#if defined(__GNUC__) || defined(__clang__)
extern void sanitizer_set_death_callback(void (*callback)(void)) __attribute__((weak));
#define HAVE_SANITIZER_DEATH_CALLBACK 1
#else
#define HAVE_SANITIZER_DEATH_CALLBACK 0
#endif

static int choose_terminal_recovery_fd(void) {
#ifdef _WIN32
    return STDOUT_FILENO;
#else
    if (isatty(STDOUT_FILENO)) {
        return STDOUT_FILENO;
    }
    if (isatty(STDERR_FILENO)) {
        return STDERR_FILENO;
    }

#ifdef O_CLOEXEC
    int tty_fd = open("/dev/tty", O_WRONLY | O_CLOEXEC);
#else
    int tty_fd = open("/dev/tty", O_WRONLY);
#endif
    if (tty_fd >= 0) {
        return tty_fd;
    }

    return STDOUT_FILENO;
#endif
}

static void terminal_recovery_callback(void) {
    if (terminal_recovery_armed) {
        terminal_restore_after_crash();
    }
}

void asan_on_error(void) {
    terminal_recovery_callback();
}

static void terminal_write_fd(int fd, const char *data, size_t size) {
#ifdef _WIN32
    if (fd == STDOUT_FILENO) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
            DWORD written;
            size_t remaining = size;
            while (remaining > 0) {
                DWORD to_write = remaining > (DWORD)0xFFFFFFFF ? (DWORD)0xFFFFFFFF : (DWORD)remaining;
                if (!WriteFile(hOut, data, to_write, &written, NULL)) {
                    break;
                }
                data += written;
                remaining -= written;
            }
            return;
        }
    }
#endif
    size_t remaining = size;
    while (remaining > 0) {
        const ssize_t written = write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        data += written;
        remaining -= (size_t)written;
    }
}

void safe_write(const char *data, size_t size) {
    terminal_write_fd(STDOUT_FILENO, data, size);
}

void draw_status_bar(float fps, float speed, const float *pos, const char *animation_name) {
    uint32_t cols, rows;
    get_terminal_size(&cols, &rows);
    if (rows == 0)
        return;

    char buffer[512];
    char anim_part[128] = "";
    if (animation_name && animation_name[0]) {
        snprintf(anim_part, sizeof(anim_part), " | ANIM: %s", animation_name);
    }

#ifdef _WIN32
    const int len = snprintf(buffer, sizeof(buffer),
                       "\x1b[%u;1H\x1b[2K\x1b[7m FPS: %.1f | SPEED: "
                       "%.2f | POS: %.2f, %.2f, %.2f%s \x1b[0m\x1b[H",
                       rows, fps, speed, pos[0], pos[1], pos[2], anim_part);
#else
    int len = snprintf(buffer, sizeof(buffer),
                       "\x1b[?2026h\x1b[%u;1H\x1b[2K\x1b[7m FPS: %.1f | SPEED: "
                       "%.2f | POS: %.2f, %.2f, %.2f%s \x1b[0m\x1b[H\x1b[?2026l",
                       rows, fps, speed, pos[0], pos[1], pos[2], anim_part);
#endif
    if (len > 0) {
        size_t written = (size_t)len;
        if (written >= sizeof(buffer)) {
            written = sizeof(buffer) - 1;
        }
        safe_write(buffer, written);
    }
}

bool termios_state_init(TermiosState *state, int fd) {
#ifdef _WIN32
    (void)fd;
    memset(state, 0, sizeof(*state));
    state->handle = GetStdHandle(STD_INPUT_HANDLE);
    if (state->handle == INVALID_HANDLE_VALUE || state->handle == NULL) {
        return false;
    }
    if (!GetConsoleMode(state->handle, &state->saved_mode)) {
        return false;
    }
    state->mode = state->saved_mode;
    state->valid = true;
    return true;
#else
    state->fd = fd;
    if (tcgetattr(fd, &state->saved) == -1)
        return false;
    state->settings = state->saved;
    return true;
#endif
}

bool termios_state_apply(TermiosState *state) {
#ifdef _WIN32
    if (!state->valid) {
        return false;
    }
    return SetConsoleMode(state->handle, state->mode) != 0;
#else
    return tcsetattr(state->fd, TCSAFLUSH, &state->settings) != -1;
#endif
}

void termios_state_restore(TermiosState *state) {
#ifdef _WIN32
    if (!state->valid) {
        return;
    }
    SetConsoleMode(state->handle, state->saved_mode);
#else
    tcsetattr(state->fd, TCSAFLUSH, &state->saved);
#endif
}

bool terminal_begin_query_mode(TermiosState *state) {
    if (!termios_state_init(state, STDIN_FILENO)) {
        return false;
    }
#ifdef _WIN32
    state->mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    state->mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
#else
    state->settings.c_lflag &= ~(ICANON | ECHO);
    state->settings.c_cc[VMIN] = 0;
    state->settings.c_cc[VTIME] = 1;
#endif
    return termios_state_apply(state);
}

void terminal_end_query_mode(TermiosState *state) {
    termios_state_restore(state);
}

void enable_raw_mode(void) {
    if (raw_mode_enabled)
        return;

    if (!termios_state_init(&raw_mode_state, STDIN_FILENO))
        return;
#ifdef _WIN32
    raw_mode_state.mode &=
        ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    raw_mode_state.mode |=
        ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT;
#else
    raw_mode_state.settings.c_lflag &= ~(ECHO | ICANON);
    raw_mode_state.settings.c_cc[VMIN] = 0;
    raw_mode_state.settings.c_cc[VTIME] = 0;
#endif
    if (!termios_state_apply(&raw_mode_state))
        return;

#ifdef _WIN32
    raw_mode_output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (raw_mode_output_handle != INVALID_HANDLE_VALUE && raw_mode_output_handle != NULL) {
        DWORD mode = 0;
        if (GetConsoleMode(raw_mode_output_handle, &mode)) {
            raw_mode_output_saved = mode;
            raw_mode_output_saved_valid = true;
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(raw_mode_output_handle, mode);
        }
    }

    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        raw_mode_input_code_page = GetConsoleCP();
        raw_mode_output_code_page = GetConsoleOutputCP();
        if (raw_mode_input_code_page != 0 && raw_mode_output_code_page != 0) {
            raw_mode_code_pages_saved = true;
            SetConsoleCP(CP_UTF8);
            SetConsoleOutputCP(CP_UTF8);
        }
    }
#endif
    raw_mode_enabled = true;
}

void terminal_set_mouse_input_enabled(const bool enabled) {
#ifdef _WIN32
    const HANDLE input_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (input_handle == INVALID_HANDLE_VALUE || input_handle == NULL) {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(input_handle, &mode)) {
        return;
    }

    mode |= ENABLE_EXTENDED_FLAGS;
    mode &= ~ENABLE_QUICK_EDIT_MODE;
    if (enabled) {
        mode |= ENABLE_MOUSE_INPUT;
    } else {
        mode &= ~ENABLE_MOUSE_INPUT;
    }
    SetConsoleMode(input_handle, mode);
#else
    (void)enabled;
#endif
}

void disable_raw_mode(void) {
    if (!raw_mode_enabled)
        return;
    termios_state_restore(&raw_mode_state);
#ifdef _WIN32
    if (raw_mode_code_pages_saved) {
        SetConsoleCP(raw_mode_input_code_page);
        SetConsoleOutputCP(raw_mode_output_code_page);
        raw_mode_code_pages_saved = false;
    }
    if (raw_mode_output_saved_valid && raw_mode_output_handle != INVALID_HANDLE_VALUE &&
        raw_mode_output_handle != NULL) {
        SetConsoleMode(raw_mode_output_handle, raw_mode_output_saved);
        raw_mode_output_saved_valid = false;
    }
#endif
    raw_mode_enabled = false;
}

void terminal_arm_recovery(void) {
    terminal_recovery_fd = choose_terminal_recovery_fd();
    terminal_recovery_armed = 1;
#if HAVE_SANITIZER_DEATH_CALLBACK
    if (!terminal_sanitizer_callback_installed) {
        if (sanitizer_set_death_callback) {
            sanitizer_set_death_callback(terminal_recovery_callback);
        }
        terminal_sanitizer_callback_installed = true;
    }
#endif
    if (!terminal_recovery_registered) {
        atexit(terminal_recovery_callback);
        terminal_recovery_registered = true;
    }
}

void terminal_disarm_recovery(void) {
    terminal_recovery_armed = 0;
}

void write_terminal_recovery_sequence(const int fd) {
    terminal_write_fd(fd, TERMINAL_RECOVERY_SEQUENCE, sizeof(TERMINAL_RECOVERY_SEQUENCE) - 1);
}

void terminal_restore_default_state(void) {
    terminal_disarm_recovery();
    disable_raw_mode();
    write_terminal_recovery_sequence(terminal_recovery_fd);
}

void terminal_restore_after_crash(void) {
    terminal_restore_default_state();
}

ssize_t terminal_read_query(char *buffer, size_t size, char terminator) {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hIn == NULL) return -1;

    DWORD start_time = GetTickCount();
    DWORD timeout = 100;
    size_t total_read = 0;

    while (total_read < size) {
        if (GetTickCount() - start_time > timeout) {
            break;
        }

        DWORD nevents = 0;
        if (!GetNumberOfConsoleInputEvents(hIn, &nevents)) {
            break;
        }

        if (nevents > 0) {
            INPUT_RECORD ir;
            DWORD read_events;
            if (ReadConsoleInputA(hIn, &ir, 1, &read_events) && read_events == 1) {
                if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                    char ch = ir.Event.KeyEvent.uChar.AsciiChar;
                    if (ch != 0) {
                        buffer[total_read++] = ch;
                        if (terminator != 0 && ch == terminator) {
                            return (ssize_t)total_read;
                        }
                    }
                }
            }
        } else {
            Sleep(5);
        }
    }
    return (ssize_t)total_read;
#else
    size_t total_read = 0;
    while (total_read < size) {
        char ch;
        ssize_t r = read(STDIN_FILENO, &ch, 1);
        if (r > 0) {
            buffer[total_read++] = ch;
            if (terminator != 0 && ch == terminator) {
                break;
            }
        } else {
            break;
        }
    }
    return (ssize_t)total_read;
#endif
}
