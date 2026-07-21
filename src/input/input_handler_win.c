#include "../core/signals.h"
#include "input_handler.h"
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct WindowsInputState {
    HANDLE input_handle;
    int last_mouse_x;
    int last_mouse_y;
    bool left_down;
    bool middle_down;
    bool right_down;
    bool prev_q;
    bool prev_m;
    bool prev_1;
    bool prev_2;
    bool prev_p;
    bool prev_a;
    bool prev_d;
    bool prev_w;
    bool prev_s;
    bool prev_e;
    bool prev_r;
    bool has_focus;
    MouseTracker mouse_track;
    char vt_buf[512];
    size_t vt_len;
} WindowsInputState;

static bool windows_key_pressed(const int vk_code) {
    return (GetAsyncKeyState(vk_code) & 0x8000) != 0;
}

static bool rising_edge(const bool down, bool *prev_state) {
    const bool edge = (down && !*prev_state) != 0;
    *prev_state = down;
    return edge;
}

static void update_windows_keyboard_state(const InputThreadData *data, WindowsInputState *state) {
    KeyState *key_state = data->key_state;
    if (!key_state) {
        return;
    }

    if (!state->has_focus) {
        memset(key_state, 0, sizeof(*key_state));
        state->prev_q = false;
        state->prev_m = false;
        state->prev_1 = false;
        state->prev_2 = false;
        state->prev_p = false;
        state->prev_a = false;
        state->prev_d = false;
        state->prev_w = false;
        state->prev_s = false;
        state->prev_e = false;
        state->prev_r = false;
        return;
    }

    const bool w_down = windows_key_pressed('W');
    const bool a_down = windows_key_pressed('A');
    const bool s_down = windows_key_pressed('S');
    const bool d_down = windows_key_pressed('D');
    const bool i_down = windows_key_pressed('I');
    const bool j_down = windows_key_pressed('J');
    const bool k_down = windows_key_pressed('K');
    const bool l_down = windows_key_pressed('L');
    const bool q_down = windows_key_pressed('Q');
    const bool m_down = windows_key_pressed('M');
    const bool v_down = windows_key_pressed('V');
    const bool b_down = windows_key_pressed('B');
    const bool e_down = windows_key_pressed('E');
    const bool r_down = windows_key_pressed('R');
    const bool one_down = windows_key_pressed('1');
    const bool two_down = windows_key_pressed('2');
    const bool p_down = windows_key_pressed('P');

    key_state->w = w_down;
    key_state->a = a_down;
    key_state->s = s_down;
    key_state->d = d_down;
    key_state->i = i_down;
    key_state->j = j_down;
    key_state->k = k_down;
    key_state->l = l_down;
    key_state->space = windows_key_pressed(VK_SPACE);
    key_state->shift = ((windows_key_pressed(VK_SHIFT) || windows_key_pressed(VK_LSHIFT) ||
                         windows_key_pressed(VK_RSHIFT)) != 0);
    key_state->ctrl = ((windows_key_pressed(VK_CONTROL) || windows_key_pressed(VK_LCONTROL) ||
                        windows_key_pressed(VK_RCONTROL)) != 0);
    key_state->q = q_down;
    key_state->v = v_down;
    key_state->b = b_down;

    if (rising_edge(q_down, &state->prev_q)) {
        signals_request_quit();
    }
    if (rising_edge(m_down, &state->prev_m)) {
        handle_key(data, 'm', 1, 1);
    }
    if (data->has_animations) {
        if (rising_edge(one_down, &state->prev_1)) {
            handle_key(data, '1', 1, 1);
        }
        if (rising_edge(two_down, &state->prev_2)) {
            handle_key(data, '2', 1, 1);
        }
        if (rising_edge(p_down, &state->prev_p)) {
            handle_key(data, 'p', 1, 1);
        }
    } else {
        state->prev_1 = one_down;
        state->prev_2 = two_down;
        state->prev_p = p_down;
    }

    if (!data->fps_controls) {
        if (rising_edge(a_down, &state->prev_a)) {
            handle_key(data, 'a', 1, 1);
        }
        if (rising_edge(d_down, &state->prev_d)) {
            handle_key(data, 'd', 1, 1);
        }
        if (rising_edge(w_down, &state->prev_w)) {
            handle_key(data, 'w', 1, 1);
        }
        if (rising_edge(s_down, &state->prev_s)) {
            handle_key(data, 's', 1, 1);
        }
        if (rising_edge(e_down, &state->prev_e)) {
            handle_key(data, 'e', 1, 1);
        }
        if (rising_edge(r_down, &state->prev_r)) {
            handle_key(data, 'r', 1, 1);
        }
    } else {
        state->prev_a = a_down;
        state->prev_d = d_down;
        state->prev_w = w_down;
        state->prev_s = s_down;
        state->prev_e = e_down;
        state->prev_r = r_down;
    }
}

static void handle_windows_mouse_event(const InputThreadData *data, WindowsInputState *state,
                                       const MOUSE_EVENT_RECORD *event) {
    if (!data->mouse_orbit) {
        return;
    }

    if (event->dwEventFlags == MOUSE_WHEELED) {
        const short wheel_delta = (short)HIWORD(event->dwButtonState);
        if (wheel_delta > 0) {
            camera_zoom(data->camera, ZOOM_AMOUNT);
        } else if (wheel_delta < 0) {
            camera_zoom(data->camera, -ZOOM_AMOUNT);
        }
        return;
    }

    const bool left_down = (event->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    const bool middle_down = (event->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0;
    const bool right_down = (event->dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;

    const int mouse_x = event->dwMousePosition.X;
    const int mouse_y = event->dwMousePosition.Y;

    if (event->dwEventFlags == 0) {
        state->last_mouse_x = mouse_x;
        state->last_mouse_y = mouse_y;
        state->left_down = left_down;
        state->middle_down = middle_down;
        state->right_down = right_down;
        return;
    }

    if (event->dwEventFlags != MOUSE_MOVED) {
        state->left_down = left_down;
        state->middle_down = middle_down;
        state->right_down = right_down;
        return;
    }

    const int dx = mouse_x - state->last_mouse_x;
    const int dy = mouse_y - state->last_mouse_y;
    state->last_mouse_x = mouse_x;
    state->last_mouse_y = mouse_y;

    if (dx != 0 || dy != 0) {
        if (left_down || state->left_down) {
            camera_orbit(data->camera, (float)dx * data->mouse_sensitivity,
                         -(float)dy * data->mouse_sensitivity);
        } else if (right_down || middle_down || state->right_down || state->middle_down) {
            const float pan_speed = data->mouse_sensitivity * 0.2F;
            camera_pan(data->camera, (float)dx * pan_speed, (float)dy * pan_speed);
        }
    }

    state->left_down = left_down;
    state->middle_down = middle_down;
    state->right_down = right_down;
}

static void process_windows_vt_bytes(InputThreadData *data, WindowsInputState *state) {
    size_t i = 0;
    const size_t n = state->vt_len;
    while (i < n) {
        if (state->vt_buf[i] != '\x1b') {
            i++;
            continue;
        }
        if (i + 1 >= n) {
            break; // lone ESC; wait for more bytes
        }
        if (state->vt_buf[i + 1] != '[') {
            i += 2;
            continue;
        }

        const size_t body = i + 2;
        size_t consumed = 0;
        const MouseCsiResult result =
            mouse_parse_csi(data, state->vt_buf + body, n - body, &consumed, &state->mouse_track);
        if (result == MOUSE_CSI_INCOMPLETE) {
            break; // need more bytes; carry from this ESC
        }
        if (result == MOUSE_CSI_HANDLED) {
            i = body + consumed;
            continue;
        }

        // Focus in/out reporting: \x1b[I / \x1b[O
        if (body < n && (state->vt_buf[body] == 'I' || state->vt_buf[body] == 'O')) {
            state->has_focus = (state->vt_buf[body] == 'I');
            i = body + 1;
            continue;
        }

        // Other CSI (keyboard) sequence: skip to its final byte (0x40-0x7E).
        size_t j = body;
        while (j < n && !((unsigned char)state->vt_buf[j] >= 0x40 &&
                          (unsigned char)state->vt_buf[j] <= 0x7E)) {
            j++;
        }
        if (j >= n) {
            break; // incomplete; carry from this ESC
        }
        i = j + 1;
    }

    const size_t remaining = n - i;
    if (remaining > 0 && i > 0) {
        memmove(state->vt_buf, state->vt_buf + i, remaining);
    }
    state->vt_len = remaining;
    if (state->vt_len >= sizeof(state->vt_buf)) {
        state->vt_len = 0; // drop a stuck oversized sequence
    }
}

static void poll_windows_console_events(InputThreadData *data, WindowsInputState *state) {
    if (state->input_handle == INVALID_HANDLE_VALUE || state->input_handle == NULL) {
        return;
    }

    // Drain the entire pending buffer each tick. In a VT terminal a single fast mouse move
    // expands to a ~13-record escape sequence, so capping the read per tick lets records
    // pile up faster than they drain and motion lags behind the cursor.
    for (;;) {
        DWORD pending_count = 0;
        if (!GetNumberOfConsoleInputEvents(state->input_handle, &pending_count) ||
            pending_count == 0) {
            break;
        }

        INPUT_RECORD records[128];
        const DWORD capacity = (DWORD)(sizeof(records) / sizeof(records[0]));
        const DWORD to_read = pending_count < capacity ? pending_count : capacity;
        DWORD read_count = 0;
        if (!ReadConsoleInput(state->input_handle, records, to_read, &read_count) ||
            read_count == 0) {
            break;
        }

        for (DWORD i = 0; i < read_count; i++) {
            if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown) {
                // Accumulate the VT input stream (mouse/focus escape sequences from terminals
                // like Windows Terminal that don't emit MOUSE_EVENT/FOCUS_EVENT records).
                const wchar_t wch = records[i].Event.KeyEvent.uChar.UnicodeChar;
                if (wch != 0 && wch < 0x80 && state->vt_len < sizeof(state->vt_buf)) {
                    state->vt_buf[state->vt_len++] = (char)wch;
                }
                continue;
            }
            if (records[i].EventType == FOCUS_EVENT) {
                state->has_focus = (records[i].Event.FocusEvent.bSetFocus != 0);
                continue;
            }
            if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                signals_request_resize();
                continue;
            }
            if (records[i].EventType == MOUSE_EVENT && data->mouse_orbit && state->has_focus) {
                handle_windows_mouse_event(data, state, &records[i].Event.MouseEvent);
            }
        }

        // Mouse/focus escape sequences accumulated above (Windows Terminal path).
        process_windows_vt_bytes(data, state);
    }
}

unsigned __stdcall input_thread_func(void *arg) {
    InputThreadData *data = (InputThreadData *)arg;
    WindowsInputState windows_state = {0};
    windows_state.has_focus = true;
    windows_state.input_handle = GetStdHandle(STD_INPUT_HANDLE);

    while (!signals_should_quit()) {
        dcat_mutex_lock(data->state_mutex);
        update_windows_keyboard_state(data, &windows_state);
        poll_windows_console_events(data, &windows_state);
        dcat_mutex_unlock(data->state_mutex);
        dcat_sleep_ms(1);
    }

    // Drop any buffered input (e.g. a trailing mouse report) so it is not echoed to the
    // shell after we restore cooked mode on exit.
    if (windows_state.input_handle != INVALID_HANDLE_VALUE && windows_state.input_handle != NULL) {
        FlushConsoleInputBuffer(windows_state.input_handle);
    }

    return 0;
}
