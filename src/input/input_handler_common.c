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
        const float dx = (float)(mx - track->last_x) * track->scale_x;
        const float dy = (float)(my - track->last_y) * track->scale_y;
        track->last_x = mx;
        track->last_y = my;
        if (dx != 0.0F || dy != 0.0F) {
            camera_orbit(data->camera, dx * data->mouse_sensitivity, -dy * data->mouse_sensitivity);
        }
        break;
    }
    case MOUSE_BUTTON_DRAG_RIGHT:
    case MOUSE_BUTTON_DRAG_MIDDLE: {
        const float dx = (float)(mx - track->last_x) * track->scale_x;
        const float dy = (float)(my - track->last_y) * track->scale_y;
        track->last_x = mx;
        track->last_y = my;
        if (dx != 0.0F || dy != 0.0F) {
            const float pan_speed = data->mouse_sensitivity * 0.2F;
            camera_pan(data->camera, dx * pan_speed, dy * pan_speed);
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

MouseCsiResult mouse_parse_csi(const InputThreadData *data, const char *buf, const size_t len,
                               size_t *consumed, MouseTracker *track) {
    if (len == 0) {
        return MOUSE_CSI_INCOMPLETE;
    }

    // SGR mouse: <btn;x;y followed by 'M' (press/motion) or 'm' (release).
    if (buf[0] == '<') {
        size_t j = 1;
        while (j < len && buf[j] != 'M' && buf[j] != 'm') {
            j++;
        }
        if (j >= len) {
            return MOUSE_CSI_INCOMPLETE;
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
        return MOUSE_CSI_HANDLED;
    }

    // Legacy X10/normal mouse: M Cb Cx Cy (each byte = value + 32).
    if (buf[0] == 'M') {
        if (len < 4) {
            return MOUSE_CSI_INCOMPLETE;
        }
        const int btn = (int)(unsigned char)buf[1] - 32;
        const int mx = (int)(unsigned char)buf[2] - 32;
        const int my = (int)(unsigned char)buf[3] - 32;
        if (data->mouse_orbit) {
            mouse_apply_action(data, btn, mx, my, track);
        }
        *consumed = 4;
        return MOUSE_CSI_HANDLED;
    }

    return MOUSE_CSI_NONE;
}

// Kitty keyboard protocol key codes for modifier keys
#define KITTY_LEFT_SHIFT 57441
#define KITTY_RIGHT_SHIFT 57447
#define KITTY_LEFT_CTRL 57442
#define KITTY_RIGHT_CTRL 57448

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

    // Discrete actions on press only
    if (event_type != 1) {
        return;
    }

    if (key_code == 'q') {
        signals_request_quit();
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
