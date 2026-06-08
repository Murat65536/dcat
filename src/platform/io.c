#include "platform/io.h"

#ifdef _WIN32
#include <limits.h>

int dcat_isatty(const int fd) {
    return _isatty(fd);
}

ssize_t dcat_read(const int fd, void *buffer, const size_t size) {
    const unsigned int chunk_size = size > UINT_MAX ? UINT_MAX : size;
    return (ssize_t)_read(fd, buffer, chunk_size);
}

ssize_t dcat_write(const int fd, const void *buffer, const size_t size) {
    const char *cursor = (const char *)buffer;
    size_t remaining = size;
    size_t total_written = 0;

    while (remaining > 0) {
        const unsigned int chunk_size = remaining > UINT_MAX ? UINT_MAX : remaining;
        const int written = _write(fd, cursor, chunk_size);
        if (written <= 0) {
            return total_written > 0 ? (ssize_t)total_written : (ssize_t)written;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
        total_written += (size_t)written;
    }

    return (ssize_t)total_written;
}

int dcat_close(const int fd) {
    return _close(fd);
}

int dcat_getpid(void) {
    return _getpid();
}

#else

int dcat_isatty(const int fd) {
    return isatty(fd);
}

ssize_t dcat_read(const int fd, void *buffer, const size_t size) {
    return read(fd, buffer, size);
}

ssize_t dcat_write(const int fd, const void *buffer, const size_t size) {
    return write(fd, buffer, size);
}

int dcat_close(const int fd) {
    return close(fd);
}

int dcat_getpid(void) {
    return getpid();
}

#endif
