#include "kitty_shm.h"
#include "terminal.h"
#include "core/platform_compat.h"
#ifdef _WIN32
#include <stdbool.h>
#include <stdint.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#endif

#ifdef _WIN32

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height) {
    (void)buffer;
    (void)width;
    (void)height;
}

bool detect_kitty_shm_support(void) {
    return false;
}

#else

static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_encode(const char *src, int len, char *dst) {
    int i = 0, j = 0;
    for (; i + 2 < len; i += 3) {
        unsigned char a = src[i], b = src[i + 1], c = src[i + 2];
        dst[j++] = b64[a >> 2];
        dst[j++] = b64[((a & 3) << 4) | (b >> 4)];
        dst[j++] = b64[((b & 0xf) << 2) | (c >> 6)];
        dst[j++] = b64[c & 0x3f];
    }
    if (i < len) {
        unsigned char a = src[i];
        dst[j++] = b64[a >> 2];
        if (i + 1 < len) {
            unsigned char b = src[i + 1];
            dst[j++] = b64[((a & 3) << 4) | (b >> 4)];
            dst[j++] = b64[((b & 0xf) << 2)];
        } else {
            dst[j++] = b64[((a & 3) << 4)];
            dst[j++] = '=';
        }
        dst[j++] = '=';
    }
    dst[j] = '\0';
    return j;
}

static pid_t kitty_pid;
static int kitty_frame;
static bool kitty_initialized = false;

static void kitty_cleanup(void) {
    if (kitty_initialized) {
        static const char *del = "\x1b_Ga=d,d=A,q=2\x1b\\";
        safe_write(del, strlen(del));
    }
}

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height) {
    if (!kitty_initialized) {
        kitty_pid = getpid();
        kitty_frame = 0;
        atexit(kitty_cleanup);
        kitty_initialized = true;
    }

    size_t data_size = (size_t)width * height * 4;

    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/dcat-%d-%d", kitty_pid, kitty_frame);
    kitty_frame++;

    int fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
    if (fd == -1) return;

    if (ftruncate(fd, (off_t)data_size) == -1) {
        close(fd);
        return;
    }

    const uint8_t *src = buffer;
    size_t remaining = data_size;
    while (remaining > 0) {
        ssize_t written = write(fd, src, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return;
        }
        src += written;
        remaining -= (size_t)written;
    }
    close(fd);

    char name_b64[128];
    int name_b64_len = b64_encode(shm_name, strlen(shm_name), name_b64);

    char cmd[512];
    int cmd_len = snprintf(cmd, sizeof(cmd),
        "\x1b[H\x1b_Gf=32,a=T,t=s,i=1,p=1,s=%u,v=%u,q=2,C=1;",
        width, height);
    memcpy(cmd + cmd_len, name_b64, name_b64_len);
    cmd_len += name_b64_len;
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
    int encoded_len = b64_encode(shm_name, strlen(shm_name), encoded_name);

    char query[256];
    int qlen = snprintf(query, sizeof(query), "\x1b_Ga=T,t=s,f=32,s=1,v=1,i=31;");
    memcpy(query + qlen, encoded_name, encoded_len);
    qlen += encoded_len;
    memcpy(query + qlen, "\x1b\\", 2);
    qlen += 2;

    static const char *cleanup = "\x1b_Ga=d,d=i,i=31\x1b\\";

    TermiosState ts;
    if (!terminal_begin_query_mode(&ts)) {
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

    terminal_end_query_mode(&ts);
    return found;
}

#endif
