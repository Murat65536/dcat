#ifndef DCAT_INPUT_HANDLER_H
#define DCAT_INPUT_HANDLER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "../graphics/camera.h"
#include "../graphics/animation.h"
#include "../renderer/vulkan_renderer.h"
#include "../graphics/model.h"

typedef struct InputThreadData {
    Camera* camera;
    VulkanRenderer* renderer;
    AnimationState* anim_state;
    Mesh* mesh;
    atomic_bool* is_focused;
    atomic_bool* running;
    bool fps_controls;
    bool has_animations;
} InputThreadData;

// Input thread function
void* input_thread_func(void* arg);

#endif
