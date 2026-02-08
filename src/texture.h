#ifndef DCAT_TEXTURE_H
#define DCAT_TEXTURE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct Texture {
    uint32_t width;
    uint32_t height;
    uint8_t* data;       // RGBA, 4 bytes per pixel
    size_t data_size;
} Texture;

// Initialize to a default 1x1 gray texture
void texture_init_default(Texture* tex);

// Create a flat normal map (blue pointing up)
void texture_create_flat_normal_map(Texture* tex);

// Load texture from file
bool texture_from_file(Texture* tex, const char* path);

// Load texture from memory buffer
bool texture_from_memory(Texture* tex, const unsigned char* buffer, size_t size);

// Free texture data
void texture_free(Texture* tex);

#endif // DCAT_TEXTURE_H
