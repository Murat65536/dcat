#include "kitty_shm.h"
#include "platform/io.h"
#include "terminal.h"
#ifdef _WIN32
#include <stdbool.h>
#include <stdint.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef _WIN32

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height,
                      bool use_hash_characters) {
    (void)buffer;
    (void)width;
    (void)height;
    (void)use_hash_characters;
}

#else

static int kitty_pid;
static int kitty_frame;
static bool kitty_initialized = false;

static void kitty_cleanup(void) {
    if (kitty_initialized) {
        static const char *del = "\x1b_Ga=d,d=A,q=2\x1b\\";
        safe_write(del, strlen(del));
    }
}

void render_kitty_shm(const uint8_t *buffer, uint32_t width, uint32_t height,
                      bool use_hash_characters) {
    (void)use_hash_characters;
    if (!kitty_initialized) {
        kitty_pid = dcat_getpid();
        kitty_frame = 0;
        atexit(kitty_cleanup);
        kitty_initialized = true;
    }

    size_t data_size = (size_t)width * height * 4;

    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/dcat-%d-%d", kitty_pid, kitty_frame);
    kitty_frame++;

    int fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
    if (fd == -1)
        return;

    if (ftruncate(fd, (off_t)data_size) == -1) {
        dcat_close(fd);
        return;
    }

    const uint8_t *src = buffer;
    size_t remaining = data_size;
    while (remaining > 0) {
        ssize_t written = dcat_write(fd, src, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            dcat_close(fd);
            return;
        }
        src += written;
        remaining -= (size_t)written;
    }
    dcat_close(fd);

    char name_b64[128];
    int name_b64_len = terminal_base64_encode(shm_name, (int)strlen(shm_name), name_b64);

    char cmd[512];
    int cmd_len = snprintf(cmd, sizeof(cmd), "\x1b[H\x1b_Gf=32,a=T,t=s,i=1,p=1,s=%u,v=%u,q=2,C=1;",
                           width, height);
    memcpy(cmd + cmd_len, name_b64, name_b64_len);
    cmd_len += name_b64_len;
    memcpy(cmd + cmd_len, "\x1b\\", 2);
    cmd_len += 2;
    safe_write(cmd, (size_t)cmd_len);
}

#endif
