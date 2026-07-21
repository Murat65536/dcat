#include "../core/signals.h"
#include "input_handler.h"
#include "platform/io.h"
#include <poll.h>
#include <string.h>

void *input_thread_func(void *arg) {
    InputThreadData *data = (InputThreadData *)arg;
    char buffer[512];
    ssize_t carry = 0;
    MouseTracker mouse_track = {0};

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

            // SGR (\x1b[<...M/m) and legacy X10 (\x1b[M...) mouse reports
            size_t mouse_consumed = 0;
            const MouseCsiResult mouse_result =
                mouse_parse_csi(data, buffer + i, (size_t)(n - i), &mouse_consumed, &mouse_track);
            if (mouse_result == MOUSE_CSI_INCOMPLETE) {
                carry = n - seq_start;
                memmove(buffer, buffer + seq_start, carry);
                break;
            }
            if (mouse_result == MOUSE_CSI_HANDLED) {
                i += (ssize_t)mouse_consumed;
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
