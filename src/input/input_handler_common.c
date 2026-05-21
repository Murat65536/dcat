#include "input_handler.h"
#include "../core/signals.h"

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
    if (event_type != 1)
        return;

    if (key_code == 'q') {
        signals_request_quit();
        return;
    }

    if (key_code == 'm') {
        const bool current = vulkan_renderer_get_wireframe_mode(data->renderer);
        vulkan_renderer_set_wireframe_mode(data->renderer, !current);
    }

    // Orbit camera controls
    if (!data->fps_controls) {
        switch (key_code) {
        case 'a':
            camera_orbit(data->camera, ROTATION_AMOUNT, 0.0f);
            break;
        case 'd':
            camera_orbit(data->camera, -ROTATION_AMOUNT, 0.0f);
            break;
        case 'w':
            camera_orbit(data->camera, 0.0f, -ROTATION_AMOUNT);
            break;
        case 's':
            camera_orbit(data->camera, 0.0f, ROTATION_AMOUNT);
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
            if (data->anim_state->current_animation_index < 0)
                data->anim_state->current_animation_index = (int)data->mesh->animations.count - 1;
            data->anim_state->current_time = 0.0f;
            break;
        case '2':
            data->anim_state->current_animation_index++;
            if (data->anim_state->current_animation_index >= (int)data->mesh->animations.count)
                data->anim_state->current_animation_index = 0;
            data->anim_state->current_time = 0.0f;
            break;
        case 'p':
            data->anim_state->playing = !data->anim_state->playing;
            break;
        default:
            break;
        }
    }
}
