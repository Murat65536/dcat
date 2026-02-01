#include "terminal.hpp"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sixel.h>
#include <cstdio>
#include <charconv>
#include <string_view>

static struct termios originalTermios;
static bool rawModeEnabled = false;

// Base64 encoding table
static const char base64Chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

namespace {

// Helper to handle EINTR and partial writes
inline void safe_write(const char* data, size_t size) {
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t written = ::write(STDOUT_FILENO, data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue; // Interrupted system call, retry
            break; // Error occurred
        }
        data += written;
        remaining -= written;
    }
}

class FastBuffer {
public:
    void ensureCapacity(size_t size) {
        if (buffer_.capacity() < size) {
            buffer_.reserve(size);
        }
        buffer_.resize(size);
        ptr_ = buffer_.data();
    }

    void append(std::string_view str) {
        std::memcpy(ptr_, str.data(), str.size());
        ptr_ += str.size();
    }

    // Optimized integer to string conversion since the numbers will only be up to 255
    void appendU8(uint8_t v) {
        ptr_ = std::to_chars(ptr_, ptr_ + 3, v).ptr;
    }

    void appendColorBlock(uint8_t rU, uint8_t gU, uint8_t bU, 
                          uint8_t rL, uint8_t gL, uint8_t bL) {
        // Foreground color
        append("\x1b[38;2;");
        appendU8(rU);
        *ptr_++ = ';';
        appendU8(gU);
        *ptr_++ = ';';
        appendU8(bU);
        
        // Background color
        append(";48;2;");
        appendU8(rL);
        *ptr_++ = ';';
        appendU8(gL);
        *ptr_++ = ';';
        appendU8(bL);
        
        // Character (upper half block)
        append("m\xE2\x96\x80");
    }

    void flush() {
        safe_write(buffer_.data(), ptr_ - buffer_.data());
    }

private:
    std::vector<char> buffer_;
    char* ptr_ = nullptr;
};

}

std::string base64Encode(const uint8_t* data, size_t length) {
    std::string result;
    result.reserve(((length + 2) / 3) * 4);
    
    for (size_t i = 0; i < length; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < length) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < length) n |= static_cast<uint32_t>(data[i + 2]);
        
        result.push_back(base64Chars[(n >> 18) & 0x3F]);
        result.push_back(base64Chars[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < length) ? base64Chars[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < length) ? base64Chars[n & 0x3F] : '=');
    }
    
    return result;
}

void renderTerminal(const uint8_t* buffer, uint32_t width, uint32_t height) {
    static FastBuffer fastBuffer;

    // Estimate buffer size:
    // Header + Footer: ~50 bytes
    // Per block: ~45 bytes max
    // Use (height + 1) / 2 to account for odd heights
    size_t estSize = 100 + (width * ((height + 1) / 2) * 45);
    fastBuffer.ensureCapacity(estSize);
    
    // Header: Synchronized update start + Home cursor
    const char* header = "\x1b[?2026h\x1b[H";
    fastBuffer.append(header);
    
    for (uint32_t y = 0; y < height; y += 2) {
        const uint8_t* rowUpper = buffer + (y * width * 4);
        const uint8_t* rowLower = buffer + ((y + 1) * width * 4);
        bool hasLowerRow = (y + 1 < height);
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t rU = rowUpper[0], gU = rowUpper[1], bU = rowUpper[2];
            rowUpper += 4;
            
            uint8_t rL = 0, gL = 0, bL = 0;
            if (hasLowerRow) {
                rL = rowLower[0]; gL = rowLower[1]; bL = rowLower[2];
                rowLower += 4;
            }
            
            fastBuffer.appendColorBlock(rU, gU, bU, rL, gL, bL);
        }
    }
    
    // Footer: Clear formatting + Synchronized update end
    const char* footer = "\x1b[0m\x1b[?2026l";
    fastBuffer.append(footer);
    
    fastBuffer.flush();
}

static int sixelWrite(char* data, int size, void* priv) {
    safe_write(data, size);
    return size;
}

void renderSixel(const uint8_t* buffer, uint32_t width, uint32_t height) {
    safe_write("\x1b[H", 3);

    sixel_output_t* output = nullptr;
    sixel_dither_t* dither = nullptr;

    if (sixel_output_new(&output, sixelWrite, nullptr, nullptr) != SIXEL_OK) {
        return;
    }

    if (sixel_dither_new(&dither, 256, nullptr) != SIXEL_OK) {
        sixel_output_unref(output);
        return;
    }

    // Create a mutable copy since sixel_dither_initialize modifies the data
    std::vector<uint8_t> pixels(buffer, buffer + width * height * 4);

    sixel_dither_initialize(dither, pixels.data(), width, height,
                            SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_NORM,
                            SIXEL_REP_CENTER_BOX, SIXEL_QUALITY_LOW);

    sixel_encode(pixels.data(), width, height, 4, dither, output);

    sixel_dither_unref(dither);
    sixel_output_unref(output);
}

void renderKittyShm(const uint8_t* buffer, uint32_t width, uint32_t height) {
    static uint32_t bufferIdx = 0;
    bufferIdx = (bufferIdx + 1) % 32;
    
    std::string shmName = "/dcat_shm_" + std::to_string(getpid()) + "_" + std::to_string(bufferIdx);
    std::string encodedName = base64Encode(
        reinterpret_cast<const uint8_t*>(shmName.c_str()), shmName.size());
    
    size_t dataSize = width * height * 4;
    
    // Create shared memory fresh each frame (Kitty unlinks it after reading)
    int fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        std::cerr << "shm_open failed: " << strerror(errno) << std::endl;
        return;
    }
    
    if (ftruncate(fd, dataSize) == -1) {
        std::cerr << "ftruncate failed: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(shmName.c_str());
        return;
    }
    
    void* ptr = mmap(nullptr, dataSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed: " << strerror(errno) << std::endl;
        close(fd);
        shm_unlink(shmName.c_str());
        return;
    }
    
    // Copy data to shared memory
    std::memcpy(ptr, buffer, dataSize);
    
    // Unmap and close fd (Kitty will shm_unlink after reading)
    munmap(ptr, dataSize);
    close(fd);
    
    // Send Kitty graphics command
    char cmd[512];
    int len = snprintf(cmd, sizeof(cmd), "\x1b_Ga=T,f=32,s=%u,v=%u,t=s,i=1,C=1,q=1;%s\x1b\\", 
                      width, height, encodedName.c_str());
    if (len > 0) {
        safe_write(cmd, static_cast<size_t>(len));
    }
}

std::pair<uint32_t, uint32_t> getTerminalSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        return { ws.ws_col, ws.ws_row };
    }
    return { DEFAULT_TERM_WIDTH, DEFAULT_TERM_HEIGHT };
}

std::pair<uint32_t, uint32_t> getTerminalSizePixels() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        return { ws.ws_xpixel, ws.ws_ypixel };
    }
    return { DEFAULT_TERM_WIDTH, DEFAULT_TERM_HEIGHT };
}

void drawStatusBar(float fps, float speed, const glm::vec3& pos, const std::string& animationName) {
    auto [cols, rows] = getTerminalSize();
    if (rows == 0) return;

    char buffer[512];
    std::string animPart = "";
    if (!animationName.empty()) {
        animPart = " | ANIM: " + animationName;
    }

    // Start sync update (\x1b[?2026h), Move to bottom row, clear line, print status, home cursor, End sync update (\x1b[?2026l)
    int len = snprintf(buffer, sizeof(buffer), 
        "\x1b[?2026h\x1b[%d;1H\x1b[2K\x1b[7m FPS: %.1f | SPEED: %.2f | POS: %.2f, %.2f, %.2f%s \x1b[0m\x1b[H\x1b[?2026l", 
        rows, fps, speed, pos.x, pos.y, pos.z, animPart.c_str());
    if (len > 0) {
        safe_write(buffer, static_cast<size_t>(len));
    }
}

std::pair<uint32_t, uint32_t> calculateRenderDimensions(
    int explicitWidth, int explicitHeight, bool useSixel, bool useKitty, bool reserveBottomLine) {
    
    if (explicitWidth > 0 && explicitHeight > 0) {
        return { static_cast<uint32_t>(explicitWidth), static_cast<uint32_t>(explicitHeight) };
    }
    
    if (useSixel || useKitty) {
        auto [pixelWidth, pixelHeight] = getTerminalSizePixels();
        if (reserveBottomLine) {
            auto [cols, rows] = getTerminalSize();
            if (rows > 0) {
                uint32_t charHeight = pixelHeight / rows;
                if (pixelHeight > charHeight) {
                    pixelHeight -= charHeight;
                }
            }
        }
        return { pixelWidth, pixelHeight };
    } else {
        auto [cols, rows] = getTerminalSize();
        if (reserveBottomLine && rows > 0) {
            rows--;
        }
        return { cols, rows * 2 };
    }
}

void enableRawMode() {
    if (rawModeEnabled) return; 
    
    tcgetattr(STDIN_FILENO, &originalTermios);
    struct termios raw = originalTermios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    rawModeEnabled = true;
}

void disableRawMode() {
    if (!rawModeEnabled) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
    rawModeEnabled = false;
}

void enterAlternateScreen() {
    safe_write("\x1b[?1049h", 8);
}

void exitAlternateScreen() {
    safe_write("\x1b[?1049l", 8);
}

void hideCursor() {
    safe_write("\x1b[?25l", 6);
}

void showCursor() {
    safe_write("\x1b[?25h", 6);
}

void enableFocusTracking() {
    safe_write("\x1b[?1004h", 8);
}

void disableFocusTracking() {
    safe_write("\x1b[?1004l", 8);
}
