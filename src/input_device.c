#include "input_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#define MAX_DEVICES 16

typedef enum DeviceType {
    DEVICE_UNKNOWN = 0,
    DEVICE_KEYBOARD,
    DEVICE_MOUSE
} DeviceType;

typedef struct DeviceInfo {
    int fd;
    unsigned long long id;
    DeviceType type;
    char path[256];
} DeviceInfo;

struct InputManager {
    struct pollfd poll_fds[MAX_DEVICES];
    size_t num_poll_fds;
    size_t num_keyboards;
    size_t num_mice;
    bool initialized;
};

static unsigned long long ev_get_id(int fd) {
    unsigned short id[4];
    if (ioctl(fd, EVIOCGID, id) == 0) {
        return ((unsigned long long)(id[ID_BUS]) << 48) |
               ((unsigned long long)(id[ID_VENDOR]) << 32) |
               ((unsigned long long)(id[ID_PRODUCT]) << 16) |
               id[ID_VERSION];
    }
    return 0;
}

static unsigned long ev_get_capabilities(int fd) {
    unsigned long bits = 0;
    return (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), &bits) >= 0) ? bits : 0;
}

static bool ev_has_key(int fd, unsigned key) {
    unsigned char bits[KEY_MAX / 8 + 1];
    memset(bits, 0, sizeof(bits));
    return (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0) &&
           (bits[key / 8] & (1 << (key % 8)));
}

static bool is_character_device(const char* path) {
    struct stat sb;
    return (stat(path, &sb) == 0) && ((sb.st_mode & S_IFMT) == S_IFCHR);
}

static DeviceType classify_device(int fd) {
    unsigned long capabilities = ev_get_capabilities(fd);
    
    if (capabilities == 0x120013 && ev_has_key(fd, KEY_ESC)) {
        return DEVICE_KEYBOARD;
    }
    
    if (capabilities == 0x17 && ev_has_key(fd, BTN_MOUSE)) {
        return DEVICE_MOUSE;
    }
    
    return DEVICE_UNKNOWN;
}

InputManager* input_manager_create(void) {
    InputManager* mgr = calloc(1, sizeof(InputManager));
    return mgr;
}

void input_manager_destroy(InputManager* mgr) {
    if (!mgr) return;
    
    for (size_t i = 0; i < mgr->num_poll_fds; i++) {
        if (mgr->poll_fds[i].fd >= 0) {
            tcflush(mgr->poll_fds[i].fd, TCIFLUSH);
            close(mgr->poll_fds[i].fd);
        }
    }
    free(mgr);
}

bool input_manager_initialize(InputManager* mgr, bool want_mice) {
    if (!mgr) return false;
    if (mgr->initialized) return mgr->num_keyboards > 0;
    
    DeviceInfo keyboards[MAX_DEVICES];
    DeviceInfo mice[MAX_DEVICES];
    size_t num_keyboards = 0;
    size_t num_mice = 0;
    
    DIR* dir = opendir("/dev/input/");
    if (!dir) return false;
    
    struct dirent* dirent;
    while ((dirent = readdir(dir))) {
        if (strncmp(dirent->d_name, "event", 5) != 0) continue;
        
        char filename[280];
        snprintf(filename, sizeof(filename), "/dev/input/%s", dirent->d_name);
        if (!is_character_device(filename)) continue;
        
        int fd = open(filename, O_RDONLY);
        if (fd < 0) continue;
        
        DeviceType type = classify_device(fd);
        if (type == DEVICE_UNKNOWN || (type == DEVICE_MOUSE && !want_mice)) {
            close(fd);
            continue;
        }
        
        DeviceInfo info;
        info.fd = fd;
        info.id = ev_get_id(fd);
        info.type = type;
        strncpy(info.path, filename, sizeof(info.path) - 1);
        info.path[sizeof(info.path) - 1] = '\0';
        
        if (type == DEVICE_KEYBOARD && num_keyboards < MAX_DEVICES) {
            keyboards[num_keyboards++] = info;
        } else if (type == DEVICE_MOUSE && num_mice < MAX_DEVICES) {
            mice[num_mice++] = info;
        } else {
            close(fd);
        }
    }
    closedir(dir);
    
    // Cull keyboards pretending to be mice
    if (want_mice) {
        for (size_t m = 0; m < num_mice; ) {
            bool duplicate = false;
            for (size_t k = 0; k < num_keyboards; k++) {
                if ((keyboards[k].id & ~0xFFFFULL) == (mice[m].id & ~0xFFFFULL)) {
                    duplicate = true;
                    break;
                }
            }
            
            if (duplicate) {
                close(mice[m].fd);
                mice[m] = mice[--num_mice];
            } else {
                m++;
            }
        }
    }
    
    if (num_keyboards == 0) {
        for (size_t i = 0; i < num_mice; i++) close(mice[i].fd);
        return false;
    }
    
    // Set up poll fds
    mgr->num_poll_fds = 0;
    for (size_t i = 0; i < num_keyboards && mgr->num_poll_fds < MAX_DEVICES; i++) {
        mgr->poll_fds[mgr->num_poll_fds].fd = keyboards[i].fd;
        mgr->poll_fds[mgr->num_poll_fds].events = POLLIN;
        mgr->poll_fds[mgr->num_poll_fds].revents = 0;
        mgr->num_poll_fds++;
    }
    mgr->num_keyboards = num_keyboards;
    
    for (size_t i = 0; i < num_mice && mgr->num_poll_fds < MAX_DEVICES; i++) {
        mgr->poll_fds[mgr->num_poll_fds].fd = mice[i].fd;
        mgr->poll_fds[mgr->num_poll_fds].events = POLLIN;
        mgr->poll_fds[mgr->num_poll_fds].revents = 0;
        mgr->num_poll_fds++;
    }
    mgr->num_mice = num_mice;
    
    // Set non-blocking
    for (size_t i = 0; i < mgr->num_poll_fds; i++) {
        int flags = fcntl(mgr->poll_fds[i].fd, F_GETFL);
        fcntl(mgr->poll_fds[i].fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    mgr->initialized = true;
    return true;
}

bool input_manager_is_available(const InputManager* mgr) {
    return mgr && mgr->initialized && mgr->num_keyboards > 0;
}

void input_manager_process_events(InputManager* mgr, KeyState* state) {
    if (!mgr || !mgr->initialized || mgr->num_poll_fds == 0) return;
    
    // Reset relative mouse movement
    state->mouse_dx = 0;
    state->mouse_dy = 0;
    
    struct input_event ev;
    
    // Process keyboards
    for (size_t i = 0; i < mgr->num_keyboards; i++) {
        while (read(mgr->poll_fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                bool pressed = (ev.value != 0);
                
                switch (ev.code) {
                    case KEY_W: state->w = pressed; break;
                    case KEY_A: state->a = pressed; break;
                    case KEY_S: state->s = pressed; break;
                    case KEY_D: state->d = pressed; break;
                    case KEY_I: state->i = pressed; break;
                    case KEY_J: state->j = pressed; break;
                    case KEY_K: state->k = pressed; break;
                    case KEY_L: state->l = pressed; break;
                    case KEY_SPACE: state->space = pressed; break;
                    case KEY_LEFTSHIFT:
                    case KEY_RIGHTSHIFT: state->shift = pressed; break;
                    case KEY_LEFTCTRL:
                    case KEY_RIGHTCTRL: state->ctrl = pressed; break;
                    case KEY_Q: state->q = pressed; break;
                    case KEY_M: state->m = pressed; break;
                    case KEY_V: state->v = pressed; break;
                    case KEY_B: state->b = pressed; break;
                    default: break;
                }
            }
        }
    }
    
    // Process mice
    for (size_t i = 0; i < mgr->num_mice; i++) {
        size_t idx = mgr->num_keyboards + i;
        while (read(mgr->poll_fds[idx].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_REL) {
                switch (ev.code) {
                    case REL_X: state->mouse_dx += ev.value; break;
                    case REL_Y: state->mouse_dy += ev.value; break;
                    default: break;
                }
            }
        }
    }
}
