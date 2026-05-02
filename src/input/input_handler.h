#ifndef DCAT_INPUT_HANDLER_H
#define DCAT_INPUT_HANDLER_H

#include "../core/threading.h"
#include "../graphics/animation.h"
#include "../graphics/camera.h"
#include "../graphics/model.h"
#include "../renderer/vulkan_renderer.h"
#include <stdbool.h>

typedef enum MouseButton {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_MIDDLE = 1,
    MOUSE_BUTTON_RIGHT = 2,
    MOUSE_BUTTON_DRAG_LEFT = 32,
    MOUSE_BUTTON_DRAG_MIDDLE = 33,
    MOUSE_BUTTON_DRAG_RIGHT = 34,
    MOUSE_BUTTON_SCROLL_UP = 64,
    MOUSE_BUTTON_SCROLL_DOWN = 65
} MouseButton;

// Key state tracking
typedef struct KeyState {
    bool w, a, s, d;
    bool i, j, k, l;
    bool space;
    bool shift;
    bool ctrl;
    bool q;
    bool m;
    bool v, b;
    int mouse_dx;
    int mouse_dy;
} KeyState;

typedef struct InputThreadData {
    Camera *camera;
    VulkanRenderer *renderer;
    AnimationState *anim_state;
    Mesh *mesh;
    DcatMutex *state_mutex;
    bool fps_controls;
    bool mouse_orbit;
    float mouse_sensitivity;
    bool has_animations;
    KeyState *key_state;
} InputThreadData;

// Input thread function
#ifdef _WIN32
unsigned __stdcall input_thread_func(void *arg);
#else
void *input_thread_func(void *arg);
#endif

#endif
