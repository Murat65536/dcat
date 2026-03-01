#include "kitty_shm.h"
#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *data, size_t len, char *out) {
    char *p = out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];

        *p++ = base64_chars[(n >> 18) & 0x3F];
        *p++ = base64_chars[(n >> 12) & 0x3F];
        *p++ = (i + 1 < len) ? base64_chars[(n >> 6) & 0x3F] : '=';
        *p++ = (i + 2 < len) ? base64_chars[n & 0x3F] : '=';
    }
    return (size_t)(p - out);
}

#define KITTY_NUM_BUFS 32

static char kitty_shm_names[KITTY_NUM_BUFS][64];
static bool kitty_shm_active[KITTY_NUM_BUFS];

static void kitty_cleanup(void) {
    for (int i = 0; i < KITTY_NUM_BUFS; i++) {
        if (kitty_shm_active[i]) {
            shm_unlink(kitty_shm_names[i]);
            kitty_shm_active[i] = false;
        }
    }
}

static bool kitty_initialized = false;

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height) {
    static uint32_t buf_idx = 0;

    if (!kitty_initialized) {
        memset(kitty_shm_active, 0, sizeof(kitty_shm_active));
        atexit(kitty_cleanup);
        kitty_initialized = true;
    }

    buf_idx = (buf_idx + 1) % KITTY_NUM_BUFS;
    size_t data_size = (size_t)width * height * 4;

    char shm_name[64];
    int name_len = snprintf(shm_name, sizeof(shm_name), "/dcat_shm_%d_%u", getpid(), buf_idx);

    char encoded_name[128];
    size_t encoded_len = base64_encode((const uint8_t *)shm_name, (size_t)name_len, encoded_name);

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return;

    if (ftruncate(fd, (off_t)data_size) == -1) {
        close(fd);
        shm_unlink(shm_name);
        return;
    }

    void *ptr = mmap(NULL, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        shm_unlink(shm_name);
        return;
    }

    memcpy(ptr, buffer, data_size);
    munmap(ptr, data_size);

    // Track for cleanup
    memcpy(kitty_shm_names[buf_idx], shm_name, (size_t)(name_len + 1));
    kitty_shm_active[buf_idx] = true;

    char cmd[512];
    int cmd_len = snprintf(cmd, sizeof(cmd),
        "\x1b_Ga=T,f=32,s=%u,v=%u,t=s,i=1,C=1,q=1;", width, height);
    memcpy(cmd + cmd_len, encoded_name, encoded_len);
    cmd_len += (int)encoded_len;
    memcpy(cmd + cmd_len, "\x1b\\", 2);
    cmd_len += 2;

    safe_write(cmd, (size_t)cmd_len);
}

bool detect_kitty_shm_support(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

    // Test using t=s (shared memory) — the same path render_kitty uses.
    // Some terminals support t=d (inline base64) but not t=s.
    static const char *shm_name = "/dcat_detect";
    static const uint8_t pixel[4] = {0, 0, 0, 0};

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return false;

    bool ok = ftruncate(fd, 4) == 0;
    if (ok) {
        void *ptr = mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr != MAP_FAILED) {
            memcpy(ptr, pixel, 4);
            munmap(ptr, 4);
        } else {
            ok = false;
        }
    }
    close(fd);
    if (!ok) {
        shm_unlink(shm_name);
        return false;
    }

    char encoded_name[64];
    size_t encoded_len = base64_encode((const uint8_t *)shm_name,
                                       strlen(shm_name), encoded_name);

    char query[256];
    int qlen = snprintf(query, sizeof(query), "\x1b_Ga=T,t=s,f=32,s=1,v=1,i=31;");
    memcpy(query + qlen, encoded_name, encoded_len);
    qlen += (int)encoded_len;
    memcpy(query + qlen, "\x1b\\", 2);
    qlen += 2;

    static const char *cleanup = "\x1b_Ga=d,d=i,i=31\x1b\\";

    TermiosState ts;
    if (!termios_state_init(&ts, STDIN_FILENO)) {
        shm_unlink(shm_name);
        return false;
    }
    ts.settings.c_lflag &= ~(ICANON | ECHO);
    ts.settings.c_cc[VMIN] = 0;
    ts.settings.c_cc[VTIME] = 1; // 100ms timeout
    if (!termios_state_apply(&ts)) {
        shm_unlink(shm_name);
        return false;
    }

    safe_write(query, (size_t)qlen);

    char buffer[32];
    bool found = false;

    ssize_t r = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
    if (r > 0) {
        buffer[r] = '\0';
        if (strstr(buffer, "\x1b_Gi=31;OK"))
            found = true;
    }

    // Safe to unlink after read — terminal has already processed the command
    shm_unlink(shm_name);

    if (found)
        safe_write(cleanup, strlen(cleanup));

    termios_state_restore(&ts);
    return found;
}
