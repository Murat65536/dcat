#include "input_device.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

namespace {

enum DeviceType {
    DEVICE_UNKNOWN = 0,
    DEVICE_KEYBOARD,
    DEVICE_MOUSE
};

struct DeviceInfo {
    int fd;
    unsigned long long id;
    DeviceType type;
    std::string path;
};

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

bool is_character_device(const std::string& path) {
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0) && ((sb.st_mode & S_IFMT) == S_IFCHR);
}

DeviceType classify_device(int fd) {
    unsigned long capabilities = ev_get_capabilities(fd);
    
    if (capabilities == 0x120013 && ev_has_key(fd, KEY_ESC)) {
        return DEVICE_KEYBOARD;
    }
    
    if (capabilities == 0x17 && ev_has_key(fd, BTN_MOUSE)) {
        return DEVICE_MOUSE;
    }
    
    return DEVICE_UNKNOWN;
}

}

InputManager::InputManager() : initialized(false) {}

InputManager::~InputManager() {
    cleanup();
}

void InputManager::cleanup() {
    for (const auto& pfd : poll_fds) {
        if (pfd.fd >= 0) {
            tcflush(pfd.fd, TCIFLUSH);
            close(pfd.fd);
        }
    }
    poll_fds.clear();
    num_keyboards = 0;
    num_mice = 0;
    initialized = false;
}

bool InputManager::initialize(bool want_mice) {
    if (initialized) return num_keyboards > 0;

    std::vector<DeviceInfo> found_keyboards;
    std::vector<DeviceInfo> found_mice;

    DIR* dir = opendir("/dev/input/");
    if (!dir) return false;

    struct dirent* dirent;
    while ((dirent = readdir(dir))) {
        if (strncmp(dirent->d_name, "event", 5) != 0) continue;

        std::string filename = std::string("/dev/input/") + dirent->d_name;
        if (!is_character_device(filename)) continue;

        int fd = open(filename.c_str(), O_RDONLY);
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
        info.path = filename;

        if (type == DEVICE_KEYBOARD) {
            found_keyboards.push_back(info);
        } else {
            found_mice.push_back(info);
        }
    }
    closedir(dir);

    // Cull keyboards pretending to be mice
    if (want_mice) {
        auto it_mouse = found_mice.begin();
        while (it_mouse != found_mice.end()) {
            bool duplicate = false;
            for (const auto& kb : found_keyboards) {
                if ((kb.id & ~0xFFFFULL) == (it_mouse->id & ~0xFFFFULL)) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                close(it_mouse->fd);
                it_mouse = found_mice.erase(it_mouse);
            } else {
                ++it_mouse;
            }
        }
    }

    if (found_keyboards.empty()) {
        for (const auto& m : found_mice) close(m.fd);
        return false;
    }

    for (const auto& kb : found_keyboards) {
        struct pollfd pfd;
        pfd.fd = kb.fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll_fds.push_back(pfd);
    }
    num_keyboards = found_keyboards.size();

    for (const auto& m : found_mice) {
        struct pollfd pfd;
        pfd.fd = m.fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll_fds.push_back(pfd);
    }
    num_mice = found_mice.size();

    for (auto& pfd : poll_fds) {
        int flags = fcntl(pfd.fd, F_GETFL);
        fcntl(pfd.fd, F_SETFL, flags | O_NONBLOCK);
    }

    initialized = true;
    return true;
}

bool InputManager::isAvailable() const {
    return initialized && num_keyboards > 0;
}

void InputManager::processEvents(KeyState& state) {
    if (!initialized || poll_fds.empty()) return;

    // Reset relative mouse movement
    state.mouse_dx = 0;
    state.mouse_dy = 0;
    
    struct input_event ev;

    // Process keyboards
    for (size_t i = 0; i < num_keyboards; ++i) {
        while (read(poll_fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                bool pressed = (ev.value != 0); // 1=press, 2=repeat, 0=release

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
                    case KEY_M: state.m = pressed; break;
                    case KEY_V: state.v = pressed; break;
                    case KEY_B: state.b = pressed; break;
                    default: break;
                }
            }
        }
    }

    // Process mice
    for (size_t i = 0; i < num_mice; ++i) {
        size_t idx = num_keyboards + i;
        while (read(poll_fds[idx].fd, &ev, sizeof(ev)) == sizeof(ev)) {
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