#pragma once

#include <cstddef>
#include <poll.h>

#ifndef MAX_OPEN_INPUT_DEVICES
#define MAX_OPEN_INPUT_DEVICES 10
#endif


#ifndef INVALID_FD
#define INVALID_FD (-1)
#endif

#ifndef INFINITE
#define INFINITE (-1)
#endif

extern struct pollfd input_devices[MAX_OPEN_INPUT_DEVICES];
extern struct pollfd* keyboards;
extern struct pollfd* mice;

extern int num_devices;
extern int num_keyboards;
extern int num_mice;

// Initialize keyboard and mouse devices
// Returns false if no keyboard found
bool initialize_devices(bool want_mice);

// Cleanup devices
void finalize_devices();

// Get device information
char* get_device_name(int fd, char* name, size_t n);
char* get_device_path(int fd, char* path, size_t n);

// Poll functions - return file descriptor of first device with input, or INVALID_FD
int poll_devices(int timeout_ms);
int poll_device(int n, int timeout_ms);
int poll_keyboards(int timeout_ms);
int poll_keyboard(int n, int timeout_ms);
int poll_mice(int timeout_ms);
int poll_mouse(int n, int timeout_ms);

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
    int mouse_dx = 0;
    int mouse_dy = 0;
};

// Process all pending input events and update key state
void process_input_events(KeyState& state);

// Check if input devices are available
bool input_devices_available();
