#include "terminal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

void get_terminal_size(uint32_t *cols, uint32_t *rows) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  } else {
    *cols = DEFAULT_TERM_WIDTH;
    *rows = DEFAULT_TERM_HEIGHT;
  }
}

void get_terminal_size_pixels(uint32_t *width, uint32_t *height) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 &&
      ws.ws_ypixel > 0) {
    *width = ws.ws_xpixel;
    *height = ws.ws_ypixel;
  } else {
    *width = DEFAULT_TERM_WIDTH;
    *height = DEFAULT_TERM_HEIGHT;
  }
}

void calculate_render_dimensions(int explicit_width, int explicit_height,
                                 bool use_sixel, bool use_kitty,
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
  *out_height = rows * 2;
}

static TermiosState raw_mode_state;
static bool raw_mode_enabled = false;

void safe_write(const char *data, size_t size) {
  size_t remaining = size;
  while (remaining > 0) {
    ssize_t written = write(STDOUT_FILENO, data, remaining);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    data += written;
    remaining -= written;
  }
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
    safe_write(buffer, (size_t)len);
  }
}

bool termios_state_init(TermiosState *state, int fd) {
  state->fd = fd;
  if (tcgetattr(fd, &state->saved) == -1)
    return false;
  state->settings = state->saved;
  return true;
}

bool termios_state_apply(TermiosState *state) {
  return tcsetattr(state->fd, TCSAFLUSH, &state->settings) != -1;
}

void termios_state_restore(TermiosState *state) {
  tcsetattr(state->fd, TCSAFLUSH, &state->saved);
}

void enable_raw_mode(void) {
  if (raw_mode_enabled)
    return;

  if (!termios_state_init(&raw_mode_state, STDIN_FILENO))
    return;
  raw_mode_state.settings.c_lflag &= ~(ECHO | ICANON);
  raw_mode_state.settings.c_cc[VMIN] = 0;
  raw_mode_state.settings.c_cc[VTIME] = 0;
  if (!termios_state_apply(&raw_mode_state))
    return;
  raw_mode_enabled = true;
}

void disable_raw_mode(void) {
  if (!raw_mode_enabled)
    return;
  termios_state_restore(&raw_mode_state);
  raw_mode_enabled = false;
}

void enter_alternate_screen(void) { safe_write("\x1b[?1049h", 8); }

void exit_alternate_screen(void) { safe_write("\x1b[?1049l", 8); }

void hide_cursor(void) { safe_write("\x1b[?25l", 6); }

void show_cursor(void) { safe_write("\x1b[?25h", 6); }

void enable_focus_tracking(void) { safe_write("\x1b[?1004h", 8); }

void disable_focus_tracking(void) { safe_write("\x1b[?1004l", 8); }
