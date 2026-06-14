#pragma once
#include "../core/threading.h"
#include "../graphics/animation.h"
#include "../graphics/camera.h"
#include "../graphics/model.h"
#include "../renderer/vulkan_renderer.h"
#include <stdbool.h>
#include <stddef.h>

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

// Shared constants for camera control
#define ROTATION_AMOUNT (GLM_PI / 8.0f)
#define ZOOM_AMOUNT 0.05f

// Shared key handler called by platform-specific implementations
void handle_key(const InputThreadData *data, int key_code, int modifiers, int event_type);

// Mouse drag/scroll position tracking, shared by the SGR and legacy X10 paths.
// scale_x/scale_y convert reported coordinate deltas to pixel-equivalent units so the same
// mouse sensitivity feels consistent whether the terminal reports pixels or character cells.
typedef struct MouseTracker {
    int last_x;
    int last_y;
    float scale_x;
    float scale_y;
} MouseTracker;

// Apply a press/motion/scroll mouse event. `btn` matches the MouseButton encoding.
void mouse_apply_action(const InputThreadData *data, int btn, int mx, int my,
                        MouseTracker *track);

typedef enum MouseCsiResult {
    MOUSE_CSI_NONE,       // not a mouse sequence
    MOUSE_CSI_HANDLED,    // mouse sequence fully parsed
    MOUSE_CSI_INCOMPLETE  // need more bytes to decide or finish
} MouseCsiResult;

// Parse a mouse report from a CSI body. `buf` points just past the "\x1b[" introducer and
// `len` is the number of available bytes. On MOUSE_CSI_HANDLED, *consumed is set to the
// number of bytes consumed from `buf`. Recognizes SGR (\x1b[<btn;x;yM/m) and legacy X10
// (\x1b[M Cb Cx Cy). Mouse actions are applied only when data->mouse_orbit is set.
MouseCsiResult mouse_parse_csi(const InputThreadData *data, const char *buf, size_t len,
                               size_t *consumed, MouseTracker *track);
