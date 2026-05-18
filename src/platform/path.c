#include "platform/path.h"

#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

bool dcat_get_executable_path(char *out, const size_t out_size) {
    if (!out || out_size == 0) {
        return false;
    }

    out[0] = '\0';

#ifdef _WIN32
    const DWORD len = GetModuleFileNameA(NULL, out, (DWORD)out_size);
    if (len == 0 || len >= out_size) {
        out[0] = '\0';
        return false;
    }
    return true;
#else
    const ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
    if (len < 0) {
        out[0] = '\0';
        return false;
    }
    out[len] = '\0';
    return true;
#endif
}

bool dcat_get_executable_directory(char *out, const size_t out_size) {
    if (!dcat_get_executable_path(out, out_size)) {
        return false;
    }

    char *last_slash = strrchr(out, '/');
    char *last_backslash = strrchr(out, '\\');
    char *last_separator = last_slash;
    if (last_backslash && (!last_separator || last_backslash > last_separator)) {
        last_separator = last_backslash;
    }

    if (!last_separator) {
        out[0] = '\0';
        return false;
    }

    last_separator[1] = '\0';
    return true;
}
