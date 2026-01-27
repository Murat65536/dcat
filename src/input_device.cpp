#include "input_device.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

// Global state
struct pollfd input_devices[MAX_OPEN_INPUT_DEVICES];
struct pollfd* keyboards = nullptr;
struct pollfd* mice = nullptr;

int num_devices = 0;
int num_keyboards = 0;
int num_mice = 0;

static bool devices_initialized = false;

namespace {

template<typename T>
void swap_values(T& a, T& b) {
    T tmp = a;
    a = b;
    b = tmp;
}

enum { KEYBOARD_DEVICE_TYPE, MOUSE_DEVICE_TYPE, NUM_DEVICE_TYPES };

struct DeviceInfo {
    unsigned long capabilities;
    int test_key;
};

DeviceInfo desired_devices[NUM_DEVICE_TYPES] = {
    { 0x120013, KEY_ESC },   // Keyboard: SYN, KEY, MSC:SCAN, LED, REP
    { 0x17, BTN_MOUSE },     // Mouse: SYN, KEY, REL, MSC:SCAN
};

bool is_character_device(const char* filename) {
    struct stat sb;
    return (stat(filename, &sb) == 0) && ((sb.st_mode & S_IFMT) == S_IFCHR);
}

unsigned long long ev_get_id(int fd) {
    unsigned short id[4];
    if (ioctl(fd, EVIOCGID, id) == 0) {
        return ((unsigned long long)(id[ID_BUS]) << 48) |
               ((unsigned long long)(id[ID_VENDOR]) << 32) |
               ((unsigned long long)(id[ID_PRODUCT]) << 16) |
               id[ID_VERSION];
    }
    return 0;
}

unsigned long ev_get_capabilities(int fd) {
    unsigned long bits = 0;
    return (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), &bits) >= 0) ? bits : 0;
}

bool ev_has_key(int fd, unsigned key) {
    unsigned char bits[KEY_MAX / 8 + 1];
    memset(bits, 0, sizeof(bits));
    return (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0) &&
           (bits[key / 8] & (1 << (key % 8)));
}

} // anonymous namespace

char* get_device_name(int fd, char* name, size_t n) {
    int count = ioctl(fd, EVIOCGNAME(n), name);
    if (count < 0) *name = '\0';
    return (count < 0) ? nullptr : name;
}

char* get_device_path(int fd, char* path, size_t n) {
    char spath[50];
    sprintf(spath, "/proc/self/fd/%d", fd);
    
    ssize_t len = readlink(spath, path, n);
    if (len < 0) {
        *path = '\0';
        return nullptr;
    }
    
    path[len] = '\0';
    return path;
}

void finalize_devices() {
    if (num_devices) {
        while (num_devices--) {
            tcflush(input_devices[num_devices].fd, TCIFLUSH);
            close(input_devices[num_devices].fd);
        }
    }
    num_devices = 0;
    num_keyboards = 0;
    num_mice = 0;
    keyboards = nullptr;
    mice = nullptr;
    devices_initialized = false;
}

bool initialize_devices(bool want_mice) {
    if (devices_initialized) return num_keyboards > 0;
    
    unsigned long long device_IDs[MAX_OPEN_INPUT_DEVICES];
    
    constexpr size_t NTH_SIZE = 128;
    char is_nth_a_mouse[NTH_SIZE] = {0};
    int nth_fd[NTH_SIZE];
    memset(nth_fd, -1, sizeof(nth_fd));
    
    // Scan /dev/input for event devices
    DIR* dir = opendir("/dev/input/");
    if (!dir) {
        return false;
    }
    
    struct dirent* dirent;
    while ((dirent = readdir(dir))) {
        if (strncmp(dirent->d_name, "event", 5) != 0) continue;
        
        int N = atoi(dirent->d_name + 5);
        if (N >= static_cast<int>(NTH_SIZE)) continue;
        
        char filename[1024] = "/dev/input/";
        strcat(filename, dirent->d_name);
        
        if (!is_character_device(filename)) continue;
        
        int fd = open(filename, O_RDONLY);
        if (fd < 0) continue;
        
        input_devices[num_devices].fd = fd;
        input_devices[num_devices].events = POLLIN;
        nth_fd[N] = fd;
        
        unsigned long device_capabilities = ev_get_capabilities(fd);
        device_IDs[num_devices] = ev_get_id(fd);
        
        bool found = false;
        for (int type = 0; type < (want_mice ? NUM_DEVICE_TYPES : 1); type++) {
            if ((device_capabilities == desired_devices[type].capabilities) &&
                ev_has_key(fd, desired_devices[type].test_key)) {
                
                is_nth_a_mouse[N] = (type == MOUSE_DEVICE_TYPE);
                
                if (is_nth_a_mouse[N]) {
                    ++num_mice;
                } else {
                    swap_values(input_devices[num_keyboards].fd, input_devices[num_devices].fd);
                    swap_values(device_IDs[num_keyboards], device_IDs[num_devices]);
                    ++num_keyboards;
                }
                
                num_devices++;
                found = true;
                break;
            }
        }
        
        if (!found) {
            close(fd);
            nth_fd[N] = INVALID_FD;
        }
    }
    
    closedir(dir);
    
    // Cull keyboards pretending to be mice
    if (want_mice) {
        for (int ik = 0; ik < num_keyboards; ik++) {
            for (int im = 0; im < num_mice; im++) {
                if ((device_IDs[ik] & ~0xFFFFULL) == (device_IDs[num_keyboards + im] & ~0xFFFFULL)) {
                    swap_values(input_devices[num_keyboards + im].fd, input_devices[num_devices - 1].fd);
                    swap_values(device_IDs[num_keyboards + im], device_IDs[num_devices - 1]);
                    close(input_devices[--num_mice, --num_devices].fd);
                }
            }
        }
    }
    
    if (!num_keyboards) {
        finalize_devices();
        return false;
    }
    
    // Sort devices for consistency
    keyboards = input_devices;
    struct pollfd* kb_ptr = input_devices;
    for (size_t n = 0; n < NTH_SIZE; n++) {
        if (nth_fd[n] >= 0 && !is_nth_a_mouse[n]) {
            (kb_ptr++)->fd = nth_fd[n];
        }
    }
    
    mice = kb_ptr;
    struct pollfd* mouse_ptr = kb_ptr;
    for (size_t n = 0; n < NTH_SIZE; n++) {
        if (nth_fd[n] >= 0 && is_nth_a_mouse[n]) {
            (mouse_ptr++)->fd = nth_fd[n];
        }
    }
    
    mice = kb_ptr;
    keyboards = input_devices;
    
    // Make devices non-blocking
    for (int n = 0; n < num_devices; n++) {
        int flags = fcntl(input_devices[n].fd, F_GETFL);
        fcntl(input_devices[n].fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    devices_initialized = true;
    return true;
}

static int poll_devices_(int begin, int end, int timeout_ms) {
    if (end < begin) swap_values(begin, end);
    if (begin == end) return INVALID_FD;
    
    for (int n = 0; n < num_devices; n++) {
        input_devices[n].revents = 0;
    }
    
    if (poll(input_devices + begin, end - begin, timeout_ms) > 0) {
        for (int n = 0; n < (end - begin); n++) {
            if (input_devices[begin + n].revents) {
                return input_devices[begin + n].fd;
            }
        }
    }
    
    return INVALID_FD;
}

int poll_devices(int timeout_ms) {
    return poll_devices_(0, num_devices, timeout_ms);
}

int poll_device(int n, int timeout_ms) {
    return ((0 <= n) && (n < num_devices))
        ? poll_devices_(n, n + 1, timeout_ms)
        : INVALID_FD;
}

int poll_keyboards(int timeout_ms) {
    return num_keyboards
        ? poll_devices_(0, num_keyboards, timeout_ms)
        : INVALID_FD;
}

int poll_keyboard(int n, int timeout_ms) {
    return ((0 <= n) && (n < num_keyboards))
        ? poll_devices_(n, n + 1, timeout_ms)
        : INVALID_FD;
}

int poll_mice(int timeout_ms) {
    return num_mice
        ? poll_devices_(num_keyboards, num_keyboards + num_mice, timeout_ms)
        : INVALID_FD;
}

int poll_mouse(int n, int timeout_ms) {
    return ((0 <= n) && (n < num_mice))
        ? poll_devices_(num_keyboards + n, num_keyboards + n + 1, timeout_ms)
        : INVALID_FD;
}

bool input_devices_available() {
    return devices_initialized && num_keyboards > 0;
}

void process_input_events(KeyState& state) {
    // Reset relative mouse movement each frame
    state.mouse_dx = 0;
    state.mouse_dy = 0;
    
    struct input_event ev;
    
    // Process all keyboard events
    for (int i = 0; i < num_keyboards; i++) {
        while (read(keyboards[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                bool pressed = (ev.value != 0);  // 1 = press, 0 = release, 2 = repeat
                
                switch (ev.code) {
                    case KEY_W: state.w = pressed; break;
                    case KEY_A: state.a = pressed; break;
                    case KEY_S: state.s = pressed; break;
                    case KEY_D: state.d = pressed; break;
                    case KEY_I: state.i = pressed; break;
                    case KEY_J: state.j = pressed; break;
                    case KEY_K: state.k = pressed; break;
                    case KEY_L: state.l = pressed; break;
                    case KEY_SPACE: state.space = pressed; break;
                    case KEY_LEFTSHIFT:
                    case KEY_RIGHTSHIFT: state.shift = pressed; break;
                    case KEY_LEFTCTRL:
                    case KEY_RIGHTCTRL: state.ctrl = pressed; break;
                    case KEY_Q: state.q = pressed; break;
                    case KEY_V: state.v = pressed; break;
                    case KEY_B: state.b = pressed; break;
                    default: break;
                }
            }
        }
    }
    
    // Process all mouse events
    for (int i = 0; i < num_mice; i++) {
        while (read(mice[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_REL) {
                switch (ev.code) {
                    case REL_X: state.mouse_dx += ev.value; break;
                    case REL_Y: state.mouse_dy += ev.value; break;
                    default: break;
                }
            }
        }
    }
}
