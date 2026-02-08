#include "input_handler.h"
#include <unistd.h>
#include <poll.h>
#include <cglm/cglm.h>

void* input_thread_func(void* arg) {
    InputThreadData* data = (InputThreadData*)arg;
    char buffer[64];
    
    while (atomic_load(data->running)) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            
            if (n > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    if (buffer[i] == 'q' || buffer[i] == 'Q') {
                        atomic_store(data->running, false);
                        return NULL;
                    }
                    
                    if (buffer[i] == 'm' || buffer[i] == 'M') {
                        bool current = vulkan_renderer_get_wireframe_mode(data->renderer);
                        vulkan_renderer_set_wireframe_mode(data->renderer, !current);
                    }
                    
                    if (!data->fps_controls) {
                        const float ROTATION_AMOUNT = GLM_PI / 8;
                        const float ZOOM_AMOUNT = 0.25f;
                        
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
                
                for (ssize_t i = 0; i < n; i++) {
                    if (buffer[i] == '\x1b' && i + 2 < n && buffer[i + 1] == '[') {
                        if (buffer[i + 2] == 'I') {
                            atomic_store(data->is_focused, true);
                        } else if (buffer[i + 2] == 'O') {
                            atomic_store(data->is_focused, false);
                        }
                    }
                }
            }
        }
        
        usleep(1000);
    }
    
    return NULL;
}
