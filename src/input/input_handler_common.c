#include "../core/signals.h"
#include "input_handler.h"

void mouse_apply_action(const InputThreadData *data, const int btn, const int mx, const int my,
                        MouseTracker *track) {
    switch (btn) {
    case MOUSE_BUTTON_LEFT:
    case MOUSE_BUTTON_MIDDLE:
    case MOUSE_BUTTON_RIGHT:
        track->last_x = mx;
        track->last_y = my;
        break;
    case MOUSE_BUTTON_DRAG_LEFT: {
        const int dx = mx - track->last_x;
        const int dy = my - track->last_y;
        track->last_x = mx;
        track->last_y = my;
        if (dx != 0 || dy != 0) {
            camera_orbit(data->camera, (float)dx * data->mouse_sensitivity,
                         -(float)dy * data->mouse_sensitivity);
        }
        break;
    }
    case MOUSE_BUTTON_DRAG_RIGHT:
    case MOUSE_BUTTON_DRAG_MIDDLE: {
        const int dx = mx - track->last_x;
        const int dy = my - track->last_y;
        track->last_x = mx;
        track->last_y = my;
        if (dx != 0 || dy != 0) {
            const float pan_speed = data->mouse_sensitivity * 0.2F;
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

// Read a base-10 unsigned integer from buf starting at *p (exclusive end `end`),
// advancing *p past the digits.
static int parse_uint(const char *buf, size_t *p, const size_t end) {
    int value = 0;
    while (*p < end && buf[*p] >= '0' && buf[*p] <= '9') {
        value = (value * 10) + (buf[*p] - '0');
        (*p)++;
    }
    return value;
}

InputCsiResult mouse_parse_csi(const InputThreadData *data, const char *buf, const size_t len,
                               size_t *consumed, MouseTracker *track) {
    if (len == 0) {
        return INPUT_CSI_INCOMPLETE;
    }

    // SGR mouse: <btn;x;y followed by 'M' (press/motion) or 'm' (release).
    if (buf[0] == '<') {
        size_t j = 1;
        while (j < len && buf[j] != 'M' && buf[j] != 'm') {
            j++;
        }
        if (j >= len) {
            return INPUT_CSI_INCOMPLETE;
        }

        size_t p = 1;
        const int btn = parse_uint(buf, &p, j);
        if (p < j && buf[p] == ';') {
            p++;
        }
        const int mx = parse_uint(buf, &p, j);
        if (p < j && buf[p] == ';') {
            p++;
        }
        const int my = parse_uint(buf, &p, j);

        if (data->mouse_orbit && buf[j] == 'M') {
            mouse_apply_action(data, btn, mx, my, track);
        }
        *consumed = j + 1;
        return INPUT_CSI_HANDLED;
    }

    // Legacy X10/normal mouse: M Cb Cx Cy (each byte = value + 32).
    if (buf[0] == 'M') {
        if (len < 4) {
            return INPUT_CSI_INCOMPLETE;
        }
        const int btn = (int)(unsigned char)buf[1] - 32;
        const int mx = (int)(unsigned char)buf[2] - 32;
        const int my = (int)(unsigned char)buf[3] - 32;
        if (data->mouse_orbit) {
            mouse_apply_action(data, btn, mx, my, track);
        }
        *consumed = 4;
        return INPUT_CSI_HANDLED;
    }

    return INPUT_CSI_NONE;
}

// Kitty keyboard protocol key codes for modifier keys
enum {
    KITTY_LEFT_SHIFT = 57441,
    KITTY_RIGHT_SHIFT = 57447,
    KITTY_LEFT_CTRL = 57442,
    KITTY_RIGHT_CTRL = 57448,
};

static bool parse_win32_key(const char *buf, const size_t len, int *key_code, int *event_type) {
    unsigned int fields[6] = {0, 0, 0, 0, 0, 1};
    size_t p = 0;

    for (size_t field = 0; field < 6; field++) {
        if (p >= len || buf[p] < '0' || buf[p] > '9') {
            return false;
        }
        while (p < len && buf[p] >= '0' && buf[p] <= '9') {
            const unsigned int digit = (unsigned int)(buf[p] - '0');
            if (fields[field] > (65535U - digit) / 10U) {
                return false;
            }
            fields[field] = fields[field] * 10U + digit;
            p++;
        }
        if (field < 5) {
            if (p >= len || buf[p] != ';') {
                return false;
            }
            p++;
        }
    }
    if (p + 1 != len || buf[p] != '_') {
        return false;
    }

    const unsigned int virtual_key = fields[0];
    const unsigned int unicode_char = fields[2];
    if (virtual_key >= 'A' && virtual_key <= 'Z') {
        *key_code = (int)(virtual_key + ('a' - 'A'));
    } else if (virtual_key == 0x10 || virtual_key == 0xA0 || virtual_key == 0xA1) {
        *key_code = KITTY_LEFT_SHIFT;
    } else if (virtual_key == 0x11 || virtual_key == 0xA2 || virtual_key == 0xA3) {
        *key_code = KITTY_LEFT_CTRL;
    } else if (unicode_char >= 'A' && unicode_char <= 'Z') {
        *key_code = (int)(unicode_char + ('a' - 'A'));
    } else {
        *key_code = (int)unicode_char;
    }
    *event_type = fields[3] == 0 ? 3 : 1;
    return true;
}

static bool parse_kitty_key(const char *buf, const size_t len, int *key_code, int *modifiers,
                            int *event_type) {
    const size_t end = len - 1;
    size_t p = 0;
    if (end == 0 || buf[p] < '0' || buf[p] > '9') {
        return false;
    }

    *key_code = parse_uint(buf, &p, end);
    while (p < end && buf[p] == ':') {
        p++;
        (void)parse_uint(buf, &p, end);
    }

    *modifiers = 1;
    *event_type = 1;
    if (p < end && buf[p] == ';') {
        p++;
        const int value = parse_uint(buf, &p, end);
        if (value > 0) {
            *modifiers = value;
        }
        if (p < end && buf[p] == ':') {
            p++;
            const int event = parse_uint(buf, &p, end);
            if (event > 0) {
                *event_type = event;
            }
        }
    }
    return true;
}

InputCsiResult input_parse_csi(const InputThreadData *data, const char *buf, const size_t len,
                               size_t *consumed, MouseTracker *track) {
    const InputCsiResult mouse = mouse_parse_csi(data, buf, len, consumed, track);
    if (mouse != INPUT_CSI_NONE) {
        return mouse;
    }

    size_t end = 0;
    while (end < len && !((unsigned char)buf[end] >= 0x40 && (unsigned char)buf[end] <= 0x7E)) {
        end++;
    }
    if (end == len) {
        return INPUT_CSI_INCOMPLETE;
    }

    *consumed = end + 1;
    int key_code = 0;
    int modifiers = 1;
    int event_type = 1;
    if (buf[end] == '_') {
        if (parse_win32_key(buf, *consumed, &key_code, &event_type)) {
            handle_key(data, key_code, modifiers, event_type);
        }
    } else if (buf[end] == 'u' || buf[end] == '~' || (buf[end] >= 'A' && buf[end] <= 'Z')) {
        if (parse_kitty_key(buf, *consumed, &key_code, &modifiers, &event_type)) {
            handle_key(data, key_code, modifiers, event_type);
        }
    }
    return INPUT_CSI_HANDLED;
}

void handle_key(const InputThreadData *data, const int key_code, const int modifiers,
                const int event_type) {
    (void)modifiers;
    const bool pressed = (event_type != 3);

    // Update FPS held-key state
    if (data->fps_controls && data->key_state) {
        switch (key_code) {
        case 'w':
            data->key_state->w = pressed;
            break;
        case 'a':
            data->key_state->a = pressed;
            break;
        case 's':
            data->key_state->s = pressed;
            break;
        case 'd':
            data->key_state->d = pressed;
            break;
        case 'i':
            data->key_state->i = pressed;
            break;
        case 'j':
            data->key_state->j = pressed;
            break;
        case 'k':
            data->key_state->k = pressed;
            break;
        case 'l':
            data->key_state->l = pressed;
            break;
        case ' ':
            data->key_state->space = pressed;
            break;
        case 'q':
            data->key_state->q = pressed;
            break;
        case 'v':
            data->key_state->v = pressed;
            break;
        case 'b':
            data->key_state->b = pressed;
            break;
        case KITTY_LEFT_SHIFT:
        case KITTY_RIGHT_SHIFT:
            data->key_state->shift = pressed;
            break;
        case KITTY_LEFT_CTRL:
        case KITTY_RIGHT_CTRL:
            data->key_state->ctrl = pressed;
            break;
        default:
            break;
        }
    }

    if (key_code == 'q' && pressed) {
        signals_request_quit();
        return;
    }

    // Discrete actions on press only
    if (event_type != 1) {
        return;
    }

    if (key_code == 'm') {
        const bool current = vulkan_renderer_get_wireframe_mode(data->renderer);
        vulkan_renderer_set_wireframe_mode(data->renderer, (!current) != 0);
    }

    // Orbit camera controls
    if (!data->fps_controls) {
        switch (key_code) {
        case 'a':
            camera_orbit(data->camera, ROTATION_AMOUNT, 0.0F);
            break;
        case 'd':
            camera_orbit(data->camera, -ROTATION_AMOUNT, 0.0F);
            break;
        case 'w':
            camera_orbit(data->camera, 0.0F, -ROTATION_AMOUNT);
            break;
        case 's':
            camera_orbit(data->camera, 0.0F, ROTATION_AMOUNT);
            break;
        case 'e':
            camera_zoom(data->camera, ZOOM_AMOUNT);
            break;
        case 'r':
            camera_zoom(data->camera, -ZOOM_AMOUNT);
            break;
        default:
            break;
        }
    }

    // Animation controls
    if (data->has_animations) {
        switch (key_code) {
        case '1':
            data->anim_state->current_animation_index--;
            if (data->anim_state->current_animation_index < 0) {
                data->anim_state->current_animation_index = (int)data->mesh->animations.count - 1;
            }
            data->anim_state->current_time = 0.0F;
            break;
        case '2':
            data->anim_state->current_animation_index++;
            if (data->anim_state->current_animation_index >= (int)data->mesh->animations.count) {
                data->anim_state->current_animation_index = 0;
            }
            data->anim_state->current_time = 0.0F;
            break;
        case 'p':
            data->anim_state->playing = ((!data->anim_state->playing) != 0);
            break;
        default:
            break;
        }
    }
}
