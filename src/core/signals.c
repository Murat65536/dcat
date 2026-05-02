#include "core/signals.h"
#include "terminal/terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#endif

// Global state for signal handlers
static atomic_bool g_running = true;
static volatile sig_atomic_t g_resize_pending = 1;
static volatile sig_atomic_t g_terminal_session_active = 0;

static void set_atomic_flag(atomic_bool *flag, const bool value) {
    *flag = value;
}

static bool get_atomic_flag(const atomic_bool *flag) {
    return *flag;
}

static void signal_handler(const int sig) {
    (void)sig;
    set_atomic_flag(&g_running, false);
}

#ifdef SIGWINCH
static void resize_handler(int sig) {
    (void)sig;
    g_resize_pending = 1;
}
#endif

static void write_signal_literal(const int fd, const char *data, size_t size) {
    while (size > 0) {
#ifdef _WIN32
#include <io.h>
        const ssize_t written = _write(fd, data, (unsigned int)size);
#else
        ssize_t written = write(fd, data, size);
#endif
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        size -= (size_t)written;
    }
}

static void write_signal_name(const int sig) {
    switch (sig) {
    case SIGABRT:
        write_signal_literal(STDERR_FILENO, "SIGABRT", 7);
        break;
#ifdef SIGBUS
    case SIGBUS:
        write_signal_literal(STDERR_FILENO, "SIGBUS", 6);
        break;
#endif
    case SIGFPE:
        write_signal_literal(STDERR_FILENO, "SIGFPE", 6);
        break;
    case SIGILL:
        write_signal_literal(STDERR_FILENO, "SIGILL", 6);
        break;
    case SIGSEGV:
        write_signal_literal(STDERR_FILENO, "SIGSEGV", 7);
        break;
    default:
        write_signal_literal(STDERR_FILENO, "unknown", 7);
        break;
    }
}

static void fatal_signal_handler(const int sig) {
    static const char prefix[] = "\r\nFatal signal: ";
    static const char suffix[] = " while rendering. Terminal recovery was attempted.\r\n";

    if (g_terminal_session_active) {
        terminal_restore_after_crash();
        g_terminal_session_active = 0;
    }

    write_signal_literal(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    write_signal_name(sig);
    write_signal_literal(STDERR_FILENO, suffix, sizeof(suffix) - 1);
    _Exit(128 + sig);
}

void signals_init(void) {
    set_atomic_flag(&g_running, true);
    g_resize_pending = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGWINCH
    signal(SIGWINCH, resize_handler);
#endif

#ifdef _WIN32
    signal(SIGABRT, fatal_signal_handler);
#ifdef SIGFPE
    signal(SIGFPE, fatal_signal_handler);
#endif
#ifdef SIGILL
    signal(SIGILL, fatal_signal_handler);
#endif
#ifdef SIGSEGV
    signal(SIGSEGV, fatal_signal_handler);
#endif
#else
    struct sigaction fatal_action = {0};
    fatal_action.sa_handler = fatal_signal_handler;
    sigemptyset(&fatal_action.sa_mask);
    sigaction(SIGABRT, &fatal_action, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &fatal_action, NULL);
#endif
    sigaction(SIGFPE, &fatal_action, NULL);
    sigaction(SIGILL, &fatal_action, NULL);
    sigaction(SIGSEGV, &fatal_action, NULL);
#endif
}

bool signals_should_quit(void) {
    return !get_atomic_flag(&g_running);
}

void signals_request_quit(void) {
    set_atomic_flag(&g_running, false);
}

bool signals_is_resize_pending(void) {
    return g_resize_pending != 0;
}

void signals_clear_resize_pending(void) {
    g_resize_pending = 0;
}

void signals_request_resize(void) {
    g_resize_pending = 1;
}

void signals_set_terminal_session_active(const bool active) {
    g_terminal_session_active = active ? 1 : 0;
}

bool signals_is_terminal_session_active(void) {
    return g_terminal_session_active != 0;
}
