#include "terminal.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sixel.h>

static struct termios originalTermios;
static bool rawModeEnabled = false;

// Base64 encoding table
static const char base64Chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

void renderTerminal(const std::vector<uint8_t>& buffer, uint32_t width, uint32_t height) {
    std::ostringstream output;
    output.str().reserve(width * (height / 2) * 28 + 100);
    
    output << "\x1b[?2026h";  // Begin synchronized update
    output << "\x1b[H";       // Home cursor
    
    size_t bufferLen = buffer.size();
    
    for (uint32_t y = 0; y < height; y += 2) {
        for (uint32_t x = 0; x < width; x++) {
            size_t idxUpper = (y * width + x) * 4;
            size_t idxLower = ((y + 1) * width + x) * 4;
            
            uint8_t rUpper = 0, gUpper = 0, bUpper = 0;
            if (idxUpper + 2 < bufferLen) {
                rUpper = buffer[idxUpper];
                gUpper = buffer[idxUpper + 1];
                bUpper = buffer[idxUpper + 2];
            }
            
            uint8_t rLower = 0, gLower = 0, bLower = 0;
            if (idxLower + 2 < bufferLen && y + 1 < height) {
                rLower = buffer[idxLower];
                gLower = buffer[idxLower + 1];
                bLower = buffer[idxLower + 2];
            }
            
            output << "\x1b[38;2;" << (int)rUpper << ";" << (int)gUpper << ";" << (int)bUpper
                   << ";48;2;" << (int)rLower << ";" << (int)gLower << ";" << (int)bLower << "m▀";
        }
    }
    
    output << "\x1b[0m";      // Clear formatting
    output << "\x1b[?2026l";  // End synchronized update
    
    std::cout << output.str() << std::flush;
}

static int sixelWrite(char* data, int size, void* priv) {
    return static_cast<int>(fwrite(data, 1, size, reinterpret_cast<FILE*>(priv)));
}

void renderSixel(const std::vector<uint8_t>& buffer, uint32_t width, uint32_t height) {
    std::cout << "\x1b[H" << std::flush;

    sixel_output_t* output = nullptr;
    sixel_dither_t* dither = nullptr;

    if (sixel_output_new(&output, sixelWrite, stdout, nullptr) != SIXEL_OK) {
        return;
    }

    if (sixel_dither_new(&dither, 256, nullptr) != SIXEL_OK) {
        sixel_output_unref(output);
        return;
    }

    // Create a mutable copy since sixel_dither_initialize modifies the data
    std::vector<uint8_t> pixels = buffer;

    sixel_dither_initialize(dither, pixels.data(), width, height,
                            SIXEL_PIXELFORMAT_RGBA8888, SIXEL_LARGE_NORM,
                            SIXEL_REP_CENTER_BOX, SIXEL_QUALITY_LOW);

    sixel_encode(pixels.data(), width, height, 4, dither, output);

    sixel_dither_unref(dither);
    sixel_output_unref(output);
    fflush(stdout);
}

void renderKittyShm(const std::vector<uint8_t>& buffer, uint32_t width, uint32_t height) {
    static std::string shmName = "/kitty_graphics_" + std::to_string(getpid());
    static std::string encodedName = base64Encode(
        reinterpret_cast<const uint8_t*>(shmName.c_str()), shmName.size());
    
    size_t dataSize = buffer.size();
    
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
    std::memcpy(ptr, buffer.data(), dataSize);
    
    // Unmap and close fd (Kitty will shm_unlink after reading)
    munmap(ptr, dataSize);
    close(fd);
    
    // Send Kitty graphics command
    std::cout << "\x1b_Ga=T,f=32,s=" << width << ",v=" << height 
              << ",t=s,i=1,C=1,q=1;" << encodedName << "\x1b\\" << std::flush;
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

std::pair<uint32_t, uint32_t> calculateRenderDimensions(
    int explicitWidth, int explicitHeight, bool useSixel, bool useKitty) {
    
    if (explicitWidth > 0 && explicitHeight > 0) {
        return { static_cast<uint32_t>(explicitWidth), static_cast<uint32_t>(explicitHeight) };
    }
    
    if (useSixel || useKitty) {
        auto [pixelWidth, pixelHeight] = getTerminalSizePixels();
        return { pixelWidth, pixelHeight };
    } else {
        auto [cols, rows] = getTerminalSize();
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
    std::cout << "\x1b[?1049h" << std::flush;
}

void exitAlternateScreen() {
    std::cout << "\x1b[?1049l" << std::flush;
}

void hideCursor() {
    std::cout << "\x1b[?25l" << std::flush;
}

void showCursor() {
    std::cout << "\x1b[?25h" << std::flush;
}

void enableFocusTracking() {
    std::cout << "\x1b[?1004h" << std::flush;
}

void disableFocusTracking() {
    std::cout << "\x1b[?1004l" << std::flush;
}
