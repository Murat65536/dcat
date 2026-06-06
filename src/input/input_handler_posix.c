#include "../core/signals.h"
#include "input_handler.h"
#include "platform/io.h"
#include <poll.h>
#include <string.h>

void *input_thread_func(void *arg) {
    InputThreadData *data = (InputThreadData *)arg;
    char buffer[512];
    ssize_t carry = 0;
    int last_mouse_x = 0, last_mouse_y = 0;

    while (!signals_should_quit()) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 1) <= 0 || !(pfd.revents & POLLIN))
            continue;

        const size_t available = sizeof(buffer) - (size_t)carry;
        ssize_t n = dcat_read(STDIN_FILENO, buffer + carry, available);
        if (n <= 0)
            continue;
        n += carry;
        carry = 0;

        dcat_mutex_lock(data->state_mutex);

        ssize_t i = 0;
        while (i < n) {
            if (buffer[i] != '\x1b') {
                // Fallback for raw bytes (Kitty protocol not active)
                if (buffer[i] == 'q' || buffer[i] == 'Q') {
                    signals_request_quit();
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
                while (j < n && buffer[j] != 'M' && buffer[j] != 'm')
                    j++;
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
                if (p < j && buffer[p] == ';')
                    p++;
                while (p < j && buffer[p] >= '0' && buffer[p] <= '9')
                    mx = mx * 10 + (buffer[p++] - '0');
                if (p < j && buffer[p] == ';')
                    p++;
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
                                camera_orbit(data->camera, (float)dx * data->mouse_sensitivity,
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
                                camera_pan(data->camera, (float)dx * pan_speed,
                                           (float)dy * pan_speed);
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
                while (i < n && buffer[i] >= '0' && buffer[i] <= '9')
                    i++;
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
                if (mod_val > 0)
                    modifiers = mod_val;
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
                    if (evt > 0)
                        event_type = evt;
                    if (i >= n) {
                        carry = n - seq_start;
                        memmove(buffer, buffer + seq_start, carry);
                        break;
                    }
                }

                // Skip ;text-as-codepoints
                if (buffer[i] == ';') {
                    i++;
                    while (i < n &&
                           !((unsigned char)buffer[i] >= 0x40 && (unsigned char)buffer[i] <= 0x7E))
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
    }

    return NULL;
}
