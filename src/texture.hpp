#pragma once

#include <vector>
#include <string>
#include <cstdint>

struct Texture {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data;
    
    Texture() : width(1), height(1), data(4, 255) {
        data[0] = 127; data[1] = 127; data[2] = 127; data[3] = 255;
    }
    Texture(uint32_t w, uint32_t h) : width(w), height(h), data(w * h * 4, 255) {}
    
    static Texture createFlatNormalMap();
    static Texture fromFile(const std::string& path);
    static Texture fromMemory(const unsigned char* data, size_t size);
};
