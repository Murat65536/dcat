#ifndef DCAT_INPUT_DEVICE_H
#define DCAT_INPUT_DEVICE_H

#include <stdbool.h>

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

typedef struct InputManager InputManager;

// Create input manager
InputManager* input_manager_create(void);

// Destroy input manager
void input_manager_destroy(InputManager* mgr);

// Initialize keyboard and mouse devices
// Returns false if no keyboard found
bool input_manager_initialize(InputManager* mgr, bool want_mice);

// Process all pending input events and update key state
void input_manager_process_events(InputManager* mgr, KeyState* state);

// Check if input devices are available
bool input_manager_is_available(const InputManager* mgr);

#endif // DCAT_INPUT_DEVICE_H
