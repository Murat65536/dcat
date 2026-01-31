#include "texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>

Texture Texture::createFlatNormalMap() {
    Texture tex;
    tex.width = 1;
    tex.height = 1;
    tex.data.resize(4);
    tex.data[0] = 127;  // R
    tex.data[1] = 127;  // G
    tex.data[2] = 255;  // B
    tex.data[3] = 255;  // A
    return tex;
}

Texture Texture::fromFile(const std::string& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        std::cerr << "Warning: Failed to load texture (" << path << "), using gray" << std::endl;
        return Texture();
    }
    
    Texture tex;
    tex.width = static_cast<uint32_t>(width);
    tex.height = static_cast<uint32_t>(height);
    tex.data.assign(data, data + width * height * 4);
    
    stbi_image_free(data);
    return tex;
}
