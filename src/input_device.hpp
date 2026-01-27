#pragma once

#include <vector>
#include <poll.h>
#include <cstddef>

// Key state tracking
struct KeyState {
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool i = false;
    bool j = false;
    bool k = false;
    bool l = false;
    bool space = false;
    bool shift = false;
    bool ctrl = false;
    bool q = false;
    bool m = false;
    bool v = false;
    bool b = false;
    int mouse_dx = 0;
    int mouse_dy = 0;
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Prevent copying to avoid double-closing FDs
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Initialize keyboard and mouse devices
    // Returns false if no keyboard found
    bool initialize(bool want_mice);

    // Process all pending input events and update key state
    void processEvents(KeyState& state);

    // Check if input devices are available
    bool isAvailable() const;

private:
    std::vector<struct pollfd> poll_fds;
    size_t num_keyboards = 0;
    size_t num_mice = 0;
    bool initialized = false;

    void cleanup();
};