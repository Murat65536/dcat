#include "input_handler.h"
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <poll.h>
#endif
#include <cglm/cglm.h>

static const float ROTATION_AMOUNT = GLM_PI / 8.0f;
static const float ZOOM_AMOUNT = 0.05f;

// Kitty keyboard protocol key codes for modifier keys
#define KITTY_LEFT_SHIFT   57441
#define KITTY_RIGHT_SHIFT  57447
#define KITTY_LEFT_CTRL    57442
#define KITTY_RIGHT_CTRL   57448

static void stop_input_loop(atomic_bool* running) {
    *running = false;
}

static bool is_input_loop_running(const atomic_bool* running) {
    return *running;
}

static void handle_key(const InputThreadData* data, const int key_code,
                       const int modifiers, const int event_type) {
    (void)modifiers;
    const bool pressed = (event_type != 3);

    // Update FPS held-key state
    if (data->fps_controls && data->key_state) {
        switch (key_code) {
            case 'w':  data->key_state->w = pressed; break;
            case 'a':  data->key_state->a = pressed; break;
            case 's':  data->key_state->s = pressed; break;
            case 'd':  data->key_state->d = pressed; break;
            case 'i':  data->key_state->i = pressed; break;
            case 'j':  data->key_state->j = pressed; break;
            case 'k':  data->key_state->k = pressed; break;
            case 'l':  data->key_state->l = pressed; break;
            case ' ':  data->key_state->space = pressed; break;
            case 'q':  data->key_state->q = pressed; break;
            case 'v':  data->key_state->v = pressed; break;
            case 'b':  data->key_state->b = pressed; break;
            case KITTY_LEFT_SHIFT: case KITTY_RIGHT_SHIFT:
                data->key_state->shift = pressed; break;
            case KITTY_LEFT_CTRL: case KITTY_RIGHT_CTRL:
                data->key_state->ctrl = pressed; break;
            default: break;
        }
    }

    // Discrete actions on press only
    if (event_type != 1) return;

    if (key_code == 'q') {
        stop_input_loop(data->running);
        return;
    }

    if (key_code == 'm') {
        const bool current = vulkan_renderer_get_wireframe_mode(data->renderer);
        vulkan_renderer_set_wireframe_mode(data->renderer, !current);
    }

    // Orbit camera controls
    if (!data->fps_controls) {
        switch (key_code) {
            case 'a': camera_orbit(data->camera, ROTATION_AMOUNT, 0.0f); break;
            case 'd': camera_orbit(data->camera, -ROTATION_AMOUNT, 0.0f); break;
            case 'w': camera_orbit(data->camera, 0.0f, -ROTATION_AMOUNT); break;
            case 's': camera_orbit(data->camera, 0.0f, ROTATION_AMOUNT); break;
            case 'e': camera_zoom(data->camera, ZOOM_AMOUNT); break;
            case 'r': camera_zoom(data->camera, -ZOOM_AMOUNT); break;
            default: break;
        }
    }

    // Animation controls
    if (data->has_animations) {
        switch (key_code) {
            case '1':
                data->anim_state->current_animation_index--;
                if (data->anim_state->current_animation_index < 0)
                    data->anim_state->current_animation_index =
                        (int)data->mesh->animations.count - 1;
                data->anim_state->current_time = 0.0f;
                break;
            case '2':
                data->anim_state->current_animation_index++;
                if (data->anim_state->current_animation_index >=
                    (int)data->mesh->animations.count)
                    data->anim_state->current_animation_index = 0;
                data->anim_state->current_time = 0.0f;
                break;
            case 'p':
                data->anim_state->playing = !data->anim_state->playing;
                break;
            default: break;
        }
    }
}

#ifdef _WIN32
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
} WindowsInputState;

static bool windows_key_pressed(int vk_code) {
    return (GetAsyncKeyState(vk_code) & 0x8000) != 0;
}

static bool rising_edge(bool down, bool* prev_state) {
    bool edge = down && !*prev_state;
    *prev_state = down;
    return edge;
}

static void update_windows_keyboard_state(const InputThreadData* data,
                                          WindowsInputState* state) {
    KeyState* key_state = data->key_state;
    if (!key_state) {
        return;
    }

    bool w_down = windows_key_pressed('W');
    bool a_down = windows_key_pressed('A');
    bool s_down = windows_key_pressed('S');
    bool d_down = windows_key_pressed('D');
    bool i_down = windows_key_pressed('I');
    bool j_down = windows_key_pressed('J');
    bool k_down = windows_key_pressed('K');
    bool l_down = windows_key_pressed('L');
    bool q_down = windows_key_pressed('Q');
    bool m_down = windows_key_pressed('M');
    bool v_down = windows_key_pressed('V');
    bool b_down = windows_key_pressed('B');
    bool e_down = windows_key_pressed('E');
    bool r_down = windows_key_pressed('R');
    bool one_down = windows_key_pressed('1');
    bool two_down = windows_key_pressed('2');
    bool p_down = windows_key_pressed('P');

    key_state->w = w_down;
    key_state->a = a_down;
    key_state->s = s_down;
    key_state->d = d_down;
    key_state->i = i_down;
    key_state->j = j_down;
    key_state->k = k_down;
    key_state->l = l_down;
    key_state->space = windows_key_pressed(VK_SPACE);
    key_state->shift = windows_key_pressed(VK_SHIFT) ||
                       windows_key_pressed(VK_LSHIFT) ||
                       windows_key_pressed(VK_RSHIFT);
    key_state->ctrl = windows_key_pressed(VK_CONTROL) ||
                      windows_key_pressed(VK_LCONTROL) ||
                      windows_key_pressed(VK_RCONTROL);
    key_state->q = q_down;
    key_state->v = v_down;
    key_state->b = b_down;

    if (rising_edge(q_down, &state->prev_q)) {
        stop_input_loop(data->running);
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

static void handle_windows_mouse_event(const InputThreadData* data,
                                       WindowsInputState* state,
                                       const MOUSE_EVENT_RECORD* event) {
    if (!data->mouse_orbit) {
        return;
    }

    if (event->dwEventFlags == MOUSE_WHEELED) {
        short wheel_delta = (short)HIWORD(event->dwButtonState);
        if (wheel_delta > 0) {
            camera_zoom(data->camera, ZOOM_AMOUNT);
        } else if (wheel_delta < 0) {
            camera_zoom(data->camera, -ZOOM_AMOUNT);
        }
        return;
    }

    bool left_down =
        (event->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    bool middle_down =
        (event->dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0;
    bool right_down =
        (event->dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;

    int mouse_x = event->dwMousePosition.X;
    int mouse_y = event->dwMousePosition.Y;

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

    int dx = mouse_x - state->last_mouse_x;
    int dy = mouse_y - state->last_mouse_y;
    state->last_mouse_x = mouse_x;
    state->last_mouse_y = mouse_y;

    if (dx != 0 || dy != 0) {
        if (left_down || state->left_down) {
            camera_orbit(data->camera,
                         (float)dx * data->mouse_sensitivity,
                         -(float)dy * data->mouse_sensitivity);
        } else if (right_down || middle_down ||
                   state->right_down || state->middle_down) {
            float pan_speed = data->mouse_sensitivity * 0.2f;
            camera_pan(data->camera, (float)dx * pan_speed, (float)dy * pan_speed);
        }
    }

    state->left_down = left_down;
    state->middle_down = middle_down;
    state->right_down = right_down;
}

static void poll_windows_console_events(InputThreadData* data,
                                        WindowsInputState* state) {
    if (state->input_handle == INVALID_HANDLE_VALUE || state->input_handle == NULL) {
        return;
    }

    DWORD pending_count = 0;
    if (!GetNumberOfConsoleInputEvents(state->input_handle, &pending_count) ||
        pending_count == 0) {
        return;
    }

    INPUT_RECORD records[16];
    DWORD to_read = pending_count < 16 ? pending_count : 16;
    DWORD read_count = 0;
    if (!ReadConsoleInput(state->input_handle, records, to_read, &read_count) ||
        read_count == 0) {
        return;
    }

    for (DWORD i = 0; i < read_count; i++) {
        if (records[i].EventType == MOUSE_EVENT && data->mouse_orbit) {
            handle_windows_mouse_event(data, state, &records[i].Event.MouseEvent);
        }
    }
}
#endif

#ifdef _WIN32
unsigned __stdcall input_thread_func(void* arg) {
#else
void* input_thread_func(void* arg) {
#endif
    InputThreadData* data = (InputThreadData*)arg;
#ifdef _WIN32
    WindowsInputState windows_state = {0};
    windows_state.input_handle = GetStdHandle(STD_INPUT_HANDLE);
#else
    char buffer[512];
    ssize_t carry = 0;
    int last_mouse_x = 0, last_mouse_y = 0;
#endif

    while (is_input_loop_running(data->running)) {
#ifdef _WIN32
        dcat_mutex_lock(data->state_mutex);
        update_windows_keyboard_state(data, &windows_state);
        poll_windows_console_events(data, &windows_state);
        dcat_mutex_unlock(data->state_mutex);
        dcat_sleep_ms(1);
#else
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 1) <= 0 || !(pfd.revents & POLLIN)) continue;

        ssize_t n = read(STDIN_FILENO, buffer + carry,
                         (ssize_t)sizeof(buffer) - carry);
        if (n <= 0) continue;
        n += carry;
        carry = 0;

        dcat_mutex_lock(data->state_mutex);

        ssize_t i = 0;
        while (i < n) {
            if (buffer[i] != '\x1b') {
                // Fallback for raw bytes (Kitty protocol not active)
                if (buffer[i] == 'q' || buffer[i] == 'Q') {
                    stop_input_loop(data->running);
                }
                i++;
                continue;
            }

            // Need at least \x1b[X
            if (i + 2 >= n) {
                carry = n - i;
                memmove(buffer, buffer + i, carry);
                break;
            }

            if (buffer[i + 1] != '[') {
                i += 2;
                continue;
            }

            ssize_t seq_start = i;
            i += 2; // past \x1b[

            // SGR mouse: \x1b[<btn;x;yM or m
            if (buffer[i] == '<') {
                i++;
                // Find terminator M or m
                ssize_t j = i;
                while (j < n && buffer[j] != 'M' && buffer[j] != 'm') j++;
                if (j >= n) {
                    carry = n - seq_start;
                    memmove(buffer, buffer + seq_start, carry);
                    break;
                }

                // Parse btn;x;y
                int btn = 0, mx = 0, my = 0;
                ssize_t p = i;
                while (p < j && buffer[p] >= '0' && buffer[p] <= '9')
                    btn = btn * 10 + (buffer[p++] - '0');
                if (p < j && buffer[p] == ';') p++;
                while (p < j && buffer[p] >= '0' && buffer[p] <= '9')
                    mx = mx * 10 + (buffer[p++] - '0');
                if (p < j && buffer[p] == ';') p++;
                while (p < j && buffer[p] >= '0' && buffer[p] <= '9')
                    my = my * 10 + (buffer[p++] - '0');

                if (data->mouse_orbit) {
                    if (buffer[j] == 'M') {
                        switch (btn) {
                            case MOUSE_BUTTON_LEFT:
                            case MOUSE_BUTTON_MIDDLE:
                            case MOUSE_BUTTON_RIGHT:
                                last_mouse_x = mx;
                                last_mouse_y = my;
                                break;
                            case MOUSE_BUTTON_DRAG_LEFT: {
                                int dx = mx - last_mouse_x;
                                int dy = my - last_mouse_y;
                                last_mouse_x = mx;
                                last_mouse_y = my;
                                if (dx != 0 || dy != 0) {
                                    camera_orbit(data->camera,
                                                 (float)dx * data->mouse_sensitivity,
                                                 -(float)dy * data->mouse_sensitivity);
                                }
                                break;
                            }
                            case MOUSE_BUTTON_DRAG_RIGHT:
                            case MOUSE_BUTTON_DRAG_MIDDLE: {
                                int dx = mx - last_mouse_x;
                                int dy = my - last_mouse_y;
                                last_mouse_x = mx;
                                last_mouse_y = my;
                                if (dx != 0 || dy != 0) {
                                    float pan_speed = data->mouse_sensitivity * 0.2f;
                                    camera_pan(data->camera, (float)dx * pan_speed, (float)dy * pan_speed);
                                }
                                break;
                            }
                            case MOUSE_BUTTON_SCROLL_UP:
                                camera_zoom(data->camera, ZOOM_AMOUNT);
                                break;
                            case MOUSE_BUTTON_SCROLL_DOWN:
                                camera_zoom(data->camera, -ZOOM_AMOUNT);
                                break;
                            default:
                                break;
                        }
                    }
                }

                i = j + 1;
                continue;
            }

            // Kitty keyboard / functional key CSI sequence
            // Format: CSI key[:shifted[:base]] [;mods[:event] [;text]] final
            int key_code = 0;
            while (i < n && buffer[i] >= '0' && buffer[i] <= '9') {
                key_code = key_code * 10 + (buffer[i] - '0');
                i++;
            }
            if (i >= n) {
                carry = n - seq_start;
                memmove(buffer, buffer + seq_start, carry);
                break;
            }

            // Skip :shifted[:base] sub-params
            while (i < n && buffer[i] == ':') {
                i++;
                while (i < n && buffer[i] >= '0' && buffer[i] <= '9') i++;
            }
            if (i >= n) {
                carry = n - seq_start;
                memmove(buffer, buffer + seq_start, carry);
                break;
            }

            int modifiers = 1;
            int event_type = 1;

            if (buffer[i] == ';') {
                i++;
                if (i >= n) {
                    carry = n - seq_start;
                    memmove(buffer, buffer + seq_start, carry);
                    break;
                }

                int mod_val = 0;
                while (i < n && buffer[i] >= '0' && buffer[i] <= '9') {
                    mod_val = mod_val * 10 + (buffer[i] - '0');
                    i++;
                }
                if (mod_val > 0) modifiers = mod_val;
                if (i >= n) {
                    carry = n - seq_start;
                    memmove(buffer, buffer + seq_start, carry);
                    break;
                }

                if (buffer[i] == ':') {
                    i++;
                    int evt = 0;
                    while (i < n && buffer[i] >= '0' && buffer[i] <= '9') {
                        evt = evt * 10 + (buffer[i] - '0');
                        i++;
                    }
                    if (evt > 0) event_type = evt;
                    if (i >= n) {
                        carry = n - seq_start;
                        memmove(buffer, buffer + seq_start, carry);
                        break;
                    }
                }

                // Skip ;text-as-codepoints
                if (buffer[i] == ';') {
                    i++;
                    while (i < n && !((unsigned char)buffer[i] >= 0x40 &&
                                      (unsigned char)buffer[i] <= 0x7E))
                        i++;
                    if (i >= n) {
                        carry = n - seq_start;
                        memmove(buffer, buffer + seq_start, carry);
                        break;
                    }
                }
            }

            // Final byte: u, ~, or A-Z
            unsigned char final_byte = (unsigned char)buffer[i];
            i++;

            if (final_byte == 'u' || final_byte == '~' ||
                (final_byte >= 'A' && final_byte <= 'Z')) {
                handle_key(data, key_code, modifiers, event_type);
            }
        }

        dcat_mutex_unlock(data->state_mutex);
#endif
    }

    return 0;
}
