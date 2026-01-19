#pragma once

#include <vector>
#include <string>
#include <cstdint>

struct Texture {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data;
    
    Texture() : width(1), height(1), data(3, 127) {}
    Texture(uint32_t w, uint32_t h) : width(w), height(h), data(w * h * 3, 127) {}
    
    static Texture createFlatNormalMap();
    static Texture fromFile(const std::string& path);
};
