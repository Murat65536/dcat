#include "kitty.h"
#include "terminal.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// raw_chunk must be a multiple of 3 to avoid mid-stream padding
#define RAW_CHUNK 3072
#define B64_CHUNK (RAW_CHUNK * 4 / 3)  // 4096

// header (max 64) + base64 payload + ST (2)
static char cmd_buf[64 + B64_CHUNK + 2];

static size_t encode_chunk(const uint8_t *data, size_t len, char *out) {
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

void render_kitty(const uint8_t *buffer, uint32_t width, uint32_t height) {
    size_t total = (size_t)width * height * 4;
    size_t offset = 0;
    bool first = true;

    while (offset < total) {
        size_t raw = total - offset;
        if (raw > RAW_CHUNK) raw = RAW_CHUNK;
        bool last = (offset + raw >= total);

        size_t b64_len = encode_chunk(buffer + offset, raw, cmd_buf + 64);

        int hdr_len;
        if (first) {
            hdr_len = snprintf(cmd_buf, 64,
                "\x1b_Ga=T,f=32,s=%u,v=%u,C=1,q=1,m=%d;",
                width, height, last ? 0 : 1);
            first = false;
        } else {
            hdr_len = snprintf(cmd_buf, 16, "\x1b_Gm=%d;", last ? 0 : 1);
        }

        // Shift b64 data to sit immediately after the header
        memmove(cmd_buf + hdr_len, cmd_buf + 64, b64_len);
        cmd_buf[hdr_len + b64_len]     = '\x1b';
        cmd_buf[hdr_len + b64_len + 1] = '\\';
        safe_write(cmd_buf, (size_t)hdr_len + b64_len + 2);

        offset += raw;
    }
}

bool detect_kitty_support(void) {
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return false;

    // 1×1 transparent RGBA pixel = 4 zero bytes = "AAAAAA==" in base64
    static const char *query   = "\x1b_Ga=T,f=32,s=1,v=1,i=31;AAAAAA==\x1b\\";
    static const char *cleanup = "\x1b_Ga=d,d=i,i=31\x1b\\";

    TermiosState ts;
    if (!termios_state_init(&ts, STDIN_FILENO))
        return false;
    ts.settings.c_lflag &= ~(ICANON | ECHO);
    ts.settings.c_cc[VMIN] = 0;
    ts.settings.c_cc[VTIME] = 1; // 100ms timeout
    if (!termios_state_apply(&ts))
        return false;

    safe_write(query, strlen(query));

    char buffer[32];
    bool found = false;

    ssize_t r = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
    if (r > 0) {
        buffer[r] = '\0';
        if (strstr(buffer, "\x1b_Gi=31;OK"))
            found = true;
    }

    if (found)
        safe_write(cleanup, strlen(cleanup));

    termios_state_restore(&ts);
    return found;
}
