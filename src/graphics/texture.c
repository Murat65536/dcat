#include "texture.h"

#include <vips/vips.h>

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

static bool texture_from_vips(Texture* tex, VipsImage* image) {
    VipsImage* srgb = NULL;
    VipsImage* rgba = NULL;

    if (vips_colourspace(image, &srgb, VIPS_INTERPRETATION_sRGB, NULL)) {
        return false;
    }

    if (vips_image_get_bands(srgb) < 4) {
        if (vips_addalpha(srgb, &rgba, NULL)) {
            g_object_unref(srgb);
            return false;
        }
        g_object_unref(srgb);
    } else {
        rgba = srgb;
    }

    size_t buf_size;
    void* buf = vips_image_write_to_memory(rgba, &buf_size);
    bool ok = false;

    if (buf) {
        tex->width = (uint32_t)vips_image_get_width(rgba);
        tex->height = (uint32_t)vips_image_get_height(rgba);
        tex->data_size = buf_size;
        tex->data = malloc(buf_size);
        if (tex->data) {
            memcpy(tex->data, buf, buf_size);
            tex->has_transparency = texture_data_has_transparency(tex->data, buf_size);
            ok = true;
        }
        g_free(buf);
    }

    g_object_unref(rgba);
    return ok;
}

bool texture_from_file(Texture* tex, const char* path) {
    VipsImage* image = vips_image_new_from_file(path, NULL);

    if (!image) {
        fprintf(stderr, "Warning: Failed to load texture (%s), using gray\n", path);
        texture_init_default(tex);
        return false;
    }

    if (!texture_from_vips(tex, image)) {
        g_object_unref(image);
        fprintf(stderr, "Warning: Failed to process texture (%s), using gray\n", path);
        texture_init_default(tex);
        return false;
    }

    g_object_unref(image);
    return true;
}

bool texture_from_memory(Texture* tex, const unsigned char* buffer, size_t size) {
    VipsImage* image = vips_image_new_from_buffer(buffer, size, "", NULL);

    if (!image) {
        fprintf(stderr, "Warning: Failed to load texture from memory, using gray\n");
        texture_init_default(tex);
        return false;
    }

    if (!texture_from_vips(tex, image)) {
        g_object_unref(image);
        fprintf(stderr, "Warning: Failed to process texture from memory, using gray\n");
        texture_init_default(tex);
        return false;
    }

    g_object_unref(image);
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
