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

static inline size_t base64_encode(const uint8_t *data, size_t len, char *out) {
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

static char kitty_shm_name[64];
static char kitty_shm_encoded[128];
static size_t kitty_shm_encoded_len;
static bool kitty_initialized = false;

static void kitty_cleanup(void) {
    if (kitty_initialized)
        shm_unlink(kitty_shm_name);
}

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height) {
    if (!kitty_initialized) {
        int name_len = snprintf(kitty_shm_name, sizeof(kitty_shm_name),
                                "/dcat_%d", getpid());
        kitty_shm_encoded_len = base64_encode(
            (const uint8_t *)kitty_shm_name, (size_t)name_len,
            kitty_shm_encoded);
        atexit(kitty_cleanup);
        kitty_initialized = true;
    }

    size_t data_size = (size_t)width * height * 4;

    int fd = shm_open(kitty_shm_name, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return;

    if (ftruncate(fd, (off_t)data_size) == -1) {
        close(fd);
        return;
    }

    void *ptr = mmap(NULL, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) return;

    memcpy(ptr, buffer, data_size);
    munmap(ptr, data_size);

    char cmd[512];
    int cmd_len = snprintf(cmd, sizeof(cmd),
        "\x1b[H\x1b_Gf=32,a=T,s=%u,v=%u,t=s,C=1;", width, height);
    memcpy(cmd + cmd_len, kitty_shm_encoded, kitty_shm_encoded_len);
    cmd_len += (int)kitty_shm_encoded_len;
    memcpy(cmd + cmd_len, "\x1b\\", 2);
    cmd_len += 2;
    safe_write(cmd, (size_t)cmd_len);
}

bool detect_kitty_shm_support(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

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

    char buf[32];
    bool found = false;

    ssize_t r = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (r > 0) {
        buf[r] = '\0';
        if (strstr(buf, "\x1b_Gi=31;OK"))
            found = true;
    }

    shm_unlink(shm_name);

    if (found)
        safe_write(cleanup, strlen(cleanup));

    termios_state_restore(&ts);
    return found;
}
