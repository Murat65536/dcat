#include "texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool texture_data_has_transparency(const uint8_t* data, size_t data_size) {
    if (!data || data_size < 4) {
        return false;
    }

    for (size_t i = 3; i < data_size; i += 4) {
        if (data[i] < 255) {
            return true;
        }
    }
    return false;
}

void texture_init_default(Texture* tex) {
    tex->width = 1;
    tex->height = 1;
    tex->data_size = 4;
    tex->has_transparency = false;
    tex->data = malloc(4);
    if (tex->data) {
        tex->data[0] = 127;  // R
        tex->data[1] = 127;  // G
        tex->data[2] = 127;  // B
        tex->data[3] = 255;  // A
    }
}

void texture_create_flat_normal_map(Texture* tex) {
    tex->width = 1;
    tex->height = 1;
    tex->data_size = 4;
    tex->has_transparency = false;
    tex->data = malloc(4);
    if (tex->data) {
        tex->data[0] = 127;  // R (neutral X)
        tex->data[1] = 127;  // G (neutral Y)
        tex->data[2] = 255;  // B (Z up)
        tex->data[3] = 255;  // A
    }
}

bool texture_from_file(Texture* tex, const char* path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
    
    if (!data) {
        fprintf(stderr, "Warning: Failed to load texture (%s), using gray\n", path);
        texture_init_default(tex);
        return false;
    }
    
    tex->width = (uint32_t)width;
    tex->height = (uint32_t)height;
    tex->data_size = (size_t)width * height * 4;
    tex->has_transparency = texture_data_has_transparency(data, tex->data_size);
    tex->data = malloc(tex->data_size);
    if (tex->data) {
        memcpy(tex->data, data, tex->data_size);
    } else {
        tex->has_transparency = false;
    }
    
    stbi_image_free(data);
    return true;
}

bool texture_from_memory(Texture* tex, const unsigned char* buffer, size_t size) {
    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(buffer, (int)size, &width, &height, &channels, 4);
    
    if (!data) {
        fprintf(stderr, "Warning: Failed to load texture from memory, using gray\n");
        texture_init_default(tex);
        return false;
    }
    
    tex->width = (uint32_t)width;
    tex->height = (uint32_t)height;
    tex->data_size = (size_t)width * height * 4;
    tex->has_transparency = texture_data_has_transparency(data, tex->data_size);
    tex->data = malloc(tex->data_size);
    if (tex->data) {
        memcpy(tex->data, data, tex->data_size);
    } else {
        tex->has_transparency = false;
    }
    
    stbi_image_free(data);
    return true;
}

void texture_free(Texture* tex) {
    free(tex->data);
    tex->data = NULL;
    tex->width = 0;
    tex->height = 0;
    tex->data_size = 0;
    tex->has_transparency = false;
}

void texture_update_transparency(Texture* tex) {
    if (!tex) {
        return;
    }
    tex->has_transparency = texture_data_has_transparency(tex->data, tex->data_size);
}
