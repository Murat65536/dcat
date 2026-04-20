#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cglm/cglm.h>

#define MAX_BONES 200
#define MAX_BONE_INFLUENCE 4
#define ALIGN_SIZE 32

// Aligned memory allocation for SIMD operations
static void* aligned_malloc(size_t size) {
    if (size == 0) return NULL;
#ifdef _WIN32
    return _aligned_malloc(size, ALIGN_SIZE);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, ALIGN_SIZE, size) != 0) return NULL;
    return ptr;
#endif
}

static void* aligned_realloc(void* ptr, const size_t old_size, const size_t new_size) {
    if (new_size == 0) {
        if (ptr) {
#ifdef _WIN32
            _aligned_free(ptr);
#else
            free(ptr);
#endif
        }
        return NULL;
    }
    if (ptr == NULL) {
        return aligned_malloc(new_size);
    }

#ifdef _WIN32
    (void)old_size;
    return _aligned_realloc(ptr, new_size, ALIGN_SIZE);
#else
    void* new_ptr = realloc(ptr, new_size);
    if (new_ptr) {
        if (((uintptr_t)new_ptr & (ALIGN_SIZE - 1)) == 0) {
            return new_ptr;
        }

        void* aligned_ptr = aligned_malloc(new_size);
        if (!aligned_ptr) {
            return new_ptr; // keep unaligned rather than losing data
        }
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(aligned_ptr, new_ptr, copy_size);
        free(new_ptr);
        return aligned_ptr;
    }
    return NULL;
#endif
}

static void* aligned_realloc_checked(void* ptr, size_t old_size,
                                            size_t new_size) {
    void* new_ptr = aligned_realloc(ptr, old_size, new_size);
    if (!new_ptr && new_size > 0) {
        fputs("Fatal: out of memory while growing dynamic array\n", stderr);
        abort();
    }
    return new_ptr;
}

static void aligned_free(void* ptr) {
    if (!ptr) return;
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// Dynamic array macros
#define ARRAY_INIT(arr) do { \
    (arr).data = NULL; \
    (arr).count = 0; \
    (arr).capacity = 0; \
} while(0)

#define ARRAY_RESERVE(arr, cap) do { \
    if ((cap) > (arr).capacity) { \
        size_t _old_cap = (arr).capacity; \
        size_t _new_cap = (cap); \
        size_t _elem_size = sizeof(*(arr).data); \
        if (_new_cap > (SIZE_MAX / _elem_size)) { \
            fputs("Fatal: dynamic array size overflow\n", stderr); \
            abort(); \
        } \
        void* _new_data = aligned_realloc_checked((arr).data, \
            _old_cap * _elem_size, _new_cap * _elem_size); \
        (arr).data = _new_data; \
        (arr).capacity = _new_cap; \
    } \
} while(0)

#define ARRAY_PUSH(arr, item) do { \
    if ((arr).count >= (arr).capacity) { \
        size_t _old_cap = (arr).capacity; \
        size_t _new_cap = _old_cap ? _old_cap * 2 : 8; \
        size_t _elem_size = sizeof(*(arr).data); \
        if (_old_cap > 0 && _new_cap < _old_cap) { \
            fputs("Fatal: dynamic array capacity overflow\n", stderr); \
            abort(); \
        } \
        if (_new_cap > (SIZE_MAX / _elem_size)) { \
            fputs("Fatal: dynamic array size overflow\n", stderr); \
            abort(); \
        } \
        void* _new_data = aligned_realloc_checked((arr).data, \
            _old_cap * _elem_size, _new_cap * _elem_size); \
        (arr).data = _new_data; \
        (arr).capacity = _new_cap; \
    } \
    (arr).data[(arr).count++] = (item); \
} while(0)

#define ARRAY_FREE(arr) do { \
    aligned_free((arr).data); \
    ARRAY_INIT(arr); \
} while(0)

// Vertex structure with bone weights for skeletal animation
typedef struct Vertex {
    vec3 position;
    vec2 texcoord;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    ivec4 bone_ids;
    vec4 bone_weights;
} Vertex;

// Dynamic array types
typedef struct VertexArray {
    Vertex* data;
    size_t count;
    size_t capacity;
} VertexArray;

typedef struct Uint32Array {
    uint32_t* data;
    size_t count;
    size_t capacity;
} Uint32Array;

typedef struct IntArray {
    int* data;
    size_t count;
    size_t capacity;
} IntArray;

typedef struct Mat4Array {
    mat4* data;
    size_t count;
    size_t capacity;
} Mat4Array;

// Sub-mesh: a range of indices sharing one material
typedef struct SubMesh {
    uint32_t index_offset;
    uint32_t index_count;
    uint32_t material_index;
} SubMesh;

typedef struct SubMeshArray {
    SubMesh* data;
    size_t count;
    size_t capacity;
} SubMeshArray;

// Alpha blending modes
typedef enum AlphaMode {
    ALPHA_MODE_OPAQUE,
    ALPHA_MODE_MASK,
    ALPHA_MODE_BLEND
} AlphaMode;

// Material information
typedef struct MaterialInfo {
    char* diffuse_path;
    char* normal_path;
    AlphaMode alpha_mode;
    float specular_strength;
    float shininess;
    float base_color[4];            // RGBA base/diffuse color factor
    unsigned int uv_channel;        // which UV set the diffuse texture uses
    unsigned char* embedded_diffuse; // raw bytes of embedded diffuse texture (or NULL)
    size_t embedded_diffuse_size;   // byte count of embedded_diffuse
    unsigned char* embedded_normal; // raw bytes of embedded normal map texture (or NULL)
    size_t embedded_normal_size;    // byte count of embedded_normal
} MaterialInfo;

// Camera setup calculated from model bounds
typedef struct CameraSetup {
    vec3 position;
    vec3 target;
    float model_scale;
} CameraSetup;

// Utility functions
static char* str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

#endif
