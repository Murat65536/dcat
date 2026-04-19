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
#endif

static const char TERMINAL_RECOVERY_SEQUENCE[] =
    "\x1b[?2026l"
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
  if (out != INVALID_HANDLE_VALUE && out != NULL &&
      GetConsoleScreenBufferInfo(out, &info)) {
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
  uint32_t cols, rows;
  get_terminal_size(&cols, &rows);
  *width = cols;
  *height = rows;
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

void calculate_render_dimensions(int explicit_width, int explicit_height,
                                 bool use_sixel, bool use_kitty,
                                 bool use_hash_characters,
                                 bool reserve_bottom_line, uint32_t *out_width,
                                 uint32_t *out_height) {
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
        uint32_t char_height = *out_height / rows;
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
#endif
static void terminal_write_fd(int fd, const char *data, size_t size);

#if defined(__GNUC__) || defined(__clang__)
extern void __sanitizer_set_death_callback(void (*callback)(void))
    __attribute__((weak));
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

void __asan_on_error(void) {
  terminal_recovery_callback();
}

static void terminal_write_fd(int fd, const char *data, size_t size) {
  size_t remaining = size;
  while (remaining > 0) {
    ssize_t written = write(fd, data, remaining);
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

void draw_status_bar(float fps, float speed, const float *pos,
                     const char *animation_name) {
  uint32_t cols, rows;
  get_terminal_size(&cols, &rows);
  if (rows == 0)
    return;

  char buffer[512];
  char anim_part[128] = "";
  if (animation_name && animation_name[0]) {
    snprintf(anim_part, sizeof(anim_part), " | ANIM: %s", animation_name);
  }

  int len = snprintf(buffer, sizeof(buffer),
                     "\x1b[?2026h\x1b[%u;1H\x1b[2K\x1b[7m FPS: %.1f | SPEED: "
                     "%.2f | POS: %.2f, %.2f, %.2f%s \x1b[0m\x1b[H\x1b[?2026l",
                     rows, fps, speed, pos[0], pos[1], pos[2], anim_part);
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
  raw_mode_state.mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  raw_mode_state.mode |= ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT;
#else
  raw_mode_state.settings.c_lflag &= ~(ECHO | ICANON);
  raw_mode_state.settings.c_cc[VMIN] = 0;
  raw_mode_state.settings.c_cc[VTIME] = 0;
#endif
  if (!termios_state_apply(&raw_mode_state))
    return;

#ifdef _WIN32
  raw_mode_output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (raw_mode_output_handle != INVALID_HANDLE_VALUE &&
      raw_mode_output_handle != NULL) {
    DWORD mode = 0;
    if (GetConsoleMode(raw_mode_output_handle, &mode)) {
      raw_mode_output_saved = mode;
      raw_mode_output_saved_valid = true;
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(raw_mode_output_handle, mode);
    }
  }
#endif
  raw_mode_enabled = true;
}

void disable_raw_mode(void) {
  if (!raw_mode_enabled)
    return;
  termios_state_restore(&raw_mode_state);
#ifdef _WIN32
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
  if (!terminal_sanitizer_callback_installed &&
      __sanitizer_set_death_callback != NULL) {
    __sanitizer_set_death_callback(terminal_recovery_callback);
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

void write_terminal_recovery_sequence(int fd) {
  terminal_write_fd(fd, TERMINAL_RECOVERY_SEQUENCE,
                    sizeof(TERMINAL_RECOVERY_SEQUENCE) - 1);
}

void terminal_restore_default_state(void) {
  terminal_disarm_recovery();
  disable_raw_mode();
  write_terminal_recovery_sequence(terminal_recovery_fd);
}

void terminal_restore_after_crash(void) {
  terminal_restore_default_state();
}
