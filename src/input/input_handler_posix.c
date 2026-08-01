#include "../core/signals.h"
#include "input_handler.h"
#include <poll.h>
#include <string.h>
#include <unistd.h>

void *input_thread_func(void *arg) {
    InputThreadData *data = (InputThreadData *)arg;
    char buffer[512];
    ssize_t carry = 0;
    MouseTracker mouse_track = {0};

    while (!signals_should_quit()) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 1) <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        const size_t available = sizeof(buffer) - (size_t)carry;
        ssize_t n = read(STDIN_FILENO, buffer + carry, available);
        if (n <= 0) {
            continue;
        }
        n += carry;
        carry = 0;

        dcat_mutex_lock(data->state_mutex);

        ssize_t i = 0;
        while (i < n) {
            if (buffer[i] != '\x1b') {
                int key_code = (unsigned char)buffer[i];
                if (key_code >= 'A' && key_code <= 'Z') {
                    key_code += 'a' - 'A';
                }
                // Legacy terminals cannot report releases, so held FPS keys require
                // Kitty or Win32-input mode. Discrete actions remain safe here.
                if (!data->fps_controls || key_code == 'q' || key_code == 'm' || key_code == '1' ||
                    key_code == '2' || key_code == 'p') {
                    handle_key(data, key_code, 1, 1);
                }
                i++;
                continue;
            }

            if (i + 1 >= n) {
                carry = n - i;
                memmove(buffer, buffer + i, carry);
                break;
            }
            if (buffer[i + 1] != '[') {
                i += 2;
                continue;
            }

            const ssize_t body = i + 2;
            size_t consumed = 0;
            const InputCsiResult result =
                input_parse_csi(data, buffer + body, (size_t)(n - body), &consumed, &mouse_track);
            if (result == INPUT_CSI_INCOMPLETE) {
                carry = n - i;
                memmove(buffer, buffer + i, carry);
                break;
            }
            i = body + (ssize_t)consumed;
        }

        dcat_mutex_unlock(data->state_mutex);
    }

    return NULL;
}
