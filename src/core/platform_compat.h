#ifndef DCAT_PLATFORM_COMPAT_H
#define DCAT_PLATFORM_COMPAT_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <process.h>
#include <windows.h>
#if defined(_MSC_VER)
#include <BaseTsd.h>

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

#ifndef isatty
#define isatty _isatty
#endif
#ifndef read
#define read _read
#endif
#ifndef write
#define write _write
#endif
#ifndef close
#define close _close
#endif
#ifndef getpid
#define getpid _getpid
#endif

#endif // _WIN32

#endif // DCAT_PLATFORM_COMPAT_H
