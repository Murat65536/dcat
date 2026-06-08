#pragma once
#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <BaseTsd.h>
#include <io.h>
#include <process.h>

#if defined(_MSC_VER)
typedef SSIZE_T ssize_t;
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#else
#include <sys/types.h>
#include <unistd.h>
#endif

int dcat_isatty(int fd);
ssize_t dcat_read(int fd, void *buffer, size_t size);
ssize_t dcat_write(int fd, const void *buffer, size_t size);
int dcat_close(int fd);
int dcat_getpid(void);
