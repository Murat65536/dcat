#include "input_handler.h"
#include <unistd.h>
#include <poll.h>
#include <cglm/cglm.h>

void* input_thread_func(void* arg) {
    InputThreadData* data = (InputThreadData*)arg;
    char buffer[64];
    bool mouse_dragging = false;
    int last_mouse_x = 0, last_mouse_y = 0;
    bool in_escape = false; // tracks escape sequences split across reads

    static const float ROTATION_AMOUNT = GLM_PI / 8.0f;
    static const float ZOOM_AMOUNT = 0.25f;
    
    while (atomic_load(data->running)) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            
            if (n > 0) {
                bool state_locked = false;
                if (!data->fps_controls || data->has_animations) {
                    pthread_mutex_lock(data->state_mutex);
                    state_locked = true;
                }

                for (ssize_t i = 0; i < n; i++) {
                    // Skip escape sequences (they're processed in the second loop).
                    // in_escape handles sequences split across read() calls.
                    if (in_escape) {
                        if (buffer[i] == 'M' || buffer[i] == 'm' ||
                            buffer[i] == 'I' || buffer[i] == 'O') {
                            in_escape = false;
                        }
                        continue;
                    }
                    if (buffer[i] == '\x1b' && i + 1 < n && buffer[i + 1] == '[') {
                        ssize_t j = i + 2;
                        // Skip to end of sequence (M, m, I, or O)
                        while (j < n && buffer[j] != 'M' && buffer[j] != 'm' && 
                               buffer[j] != 'I' && buffer[j] != 'O') {
                            j++;
                        }
                        if (j >= n) {
                            in_escape = true; // sequence continues in next read
                        }
                        i = j;
                        continue;
                    }
                    
                    if (buffer[i] == 'q' || buffer[i] == 'Q') {
                        if (state_locked) {
                            pthread_mutex_unlock(data->state_mutex);
                        }
                        atomic_store(data->running, false);
                        return NULL;
                    }
                    
                    if (buffer[i] == 'm' || buffer[i] == 'M') {
                        bool current = vulkan_renderer_get_wireframe_mode(data->renderer);
                        vulkan_renderer_set_wireframe_mode(data->renderer, !current);
                    }
                    
                    if (!data->fps_controls) {
                        if (buffer[i] == 'a' || buffer[i] == 'A') {
                            camera_orbit(data->camera, ROTATION_AMOUNT, 0.0f);
                        }
                        if (buffer[i] == 'd' || buffer[i] == 'D') {
                            camera_orbit(data->camera, -ROTATION_AMOUNT, 0.0f);
                        }
                        if (buffer[i] == 'w' || buffer[i] == 'W') {
                            camera_orbit(data->camera, 0.0f, -ROTATION_AMOUNT);
                        }
                        if (buffer[i] == 's' || buffer[i] == 'S') {
                            camera_orbit(data->camera, 0.0f, ROTATION_AMOUNT);
                        }
                        if (buffer[i] == 'e' || buffer[i] == 'E') {
                            camera_zoom(data->camera, ZOOM_AMOUNT);
                        }
                        if (buffer[i] == 'r' || buffer[i] == 'R') {
                            camera_zoom(data->camera, -ZOOM_AMOUNT);
                        }
                    }
                    
                    if (data->has_animations) {
                        if (buffer[i] == '1') {
                            data->anim_state->current_animation_index--;
                            if (data->anim_state->current_animation_index < 0) {
                                data->anim_state->current_animation_index = 
                                    (int)data->mesh->animations.count - 1;
                            }
                            data->anim_state->current_time = 0.0f;
                        } else if (buffer[i] == '2') {
                            data->anim_state->current_animation_index++;
                            if (data->anim_state->current_animation_index >= 
                                (int)data->mesh->animations.count) {
                                data->anim_state->current_animation_index = 0;
                            }
                            data->anim_state->current_time = 0.0f;
                        } else if (buffer[i] == 'p') {
                            data->anim_state->playing = !data->anim_state->playing;
                        }
                    }
                }

                if (state_locked) {
                    pthread_mutex_unlock(data->state_mutex);
                }
                
                for (ssize_t i = 0; i < n; i++) {
                    if (buffer[i] == '\x1b' && i + 2 < n && buffer[i + 1] == '[') {
                        if (buffer[i + 2] == 'I') {
                            atomic_store(data->is_focused, true);
                        } else if (buffer[i + 2] == 'O') {
                            atomic_store(data->is_focused, false);
                        } else if (data->mouse_orbit && buffer[i + 2] == '<') {
                            // SGR mouse: \x1b[<btn;x;yM (press/motion) or \x1b[<btn;x;ym (release)
                            int btn = 0, mx = 0, my = 0;
                            ssize_t j = i + 3;
                            while (j < n && buffer[j] >= '0' && buffer[j] <= '9')
                                btn = btn * 10 + (buffer[j++] - '0');
                            if (j < n && buffer[j] == ';') j++;
                            while (j < n && buffer[j] >= '0' && buffer[j] <= '9')
                                mx = mx * 10 + (buffer[j++] - '0');
                            if (j < n && buffer[j] == ';') j++;
                            while (j < n && buffer[j] >= '0' && buffer[j] <= '9')
                                my = my * 10 + (buffer[j++] - '0');
                            if (j < n && (buffer[j] == 'M' || buffer[j] == 'm')) {
                                if (buffer[j] == 'm') {
                                    mouse_dragging = false;
                                } else if (btn == 0) { // left button press
                                    mouse_dragging = true;
                                    last_mouse_x = mx;
                                    last_mouse_y = my;
                                } else if (btn == 32 && mouse_dragging) { // left drag
                                    int dx = mx - last_mouse_x;
                                    int dy = my - last_mouse_y;
                                    last_mouse_x = mx;
                                    last_mouse_y = my;
                                    if (dx != 0 || dy != 0) {
                                        pthread_mutex_lock(data->state_mutex);
                                        camera_orbit(data->camera,
                                                     (float)dx * data->mouse_sensitivity,
                                                     -(float)dy * data->mouse_sensitivity);
                                        pthread_mutex_unlock(data->state_mutex);
                                    }
                                } else if (btn == 64) { // scroll up
                                    pthread_mutex_lock(data->state_mutex);
                                    camera_zoom(data->camera, ZOOM_AMOUNT);
                                    pthread_mutex_unlock(data->state_mutex);
                                } else if (btn == 65) { // scroll down
                                    pthread_mutex_lock(data->state_mutex);
                                    camera_zoom(data->camera, -ZOOM_AMOUNT);
                                    pthread_mutex_unlock(data->state_mutex);
                                }
                                i = j;
                            }
                        }
                    }
                }
            }
        }
        
        usleep(1000);
    }
    
    return NULL;
}
