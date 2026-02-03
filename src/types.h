#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <cglm/cglm.h>

#define MAX_BONES 200
#define MAX_BONE_INFLUENCE 4

// Aligned memory allocation for AVX (32-byte alignment)
#define ALIGN_SIZE 32

static inline void* aligned_malloc(size_t size) {
    if (size == 0) return NULL;
    void* ptr = NULL;
    if (posix_memalign(&ptr, ALIGN_SIZE, size) != 0) return NULL;
    return ptr;
}

static inline void* aligned_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return aligned_malloc(new_size);
    }
    
    // Always use aligned allocation to maintain alignment guarantees
    void* new_ptr = aligned_malloc(new_size);
    if (new_ptr && old_size > 0) {
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
    }
    free(ptr);
    return new_ptr;
}

#define ARRAY_INIT(arr) do { (arr).data = NULL; (arr).count = 0; (arr).capacity = 0; } while(0)

#define ARRAY_RESERVE(arr, cap) do { \
    if ((cap) > (arr).capacity) { \
        size_t old_cap = (arr).capacity; \
        (arr).capacity = (cap); \
        (arr).data = aligned_realloc((arr).data, \
            old_cap * sizeof(*(arr).data), \
            (arr).capacity * sizeof(*(arr).data)); \
    } \
} while(0)

#define ARRAY_PUSH(arr, item) do { \
    if ((arr).count >= (arr).capacity) { \
        size_t old_cap = (arr).capacity; \
        (arr).capacity = (arr).capacity ? (arr).capacity * 2 : 8; \
        (arr).data = aligned_realloc((arr).data, \
            old_cap * sizeof(*(arr).data), \
            (arr).capacity * sizeof(*(arr).data)); \
    } \
    (arr).data[(arr).count++] = (item); \
} while(0)

#define ARRAY_FREE(arr) do { free((arr).data); ARRAY_INIT(arr); } while(0)

// Vertex structure
typedef struct Vertex {
    vec3 position;
    vec2 texcoord;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    ivec4 bone_ids;      // 4 bone indices per vertex (-1 means unused)
    vec4 bone_weights;   // 4 weights (must sum to 1.0)
} Vertex;

// Dynamic arrays for common types
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

// Alpha blending mode
typedef enum AlphaMode {
    ALPHA_MODE_OPAQUE,
    ALPHA_MODE_MASK,
    ALPHA_MODE_BLEND
} AlphaMode;

// Material information extracted from model
typedef struct MaterialInfo {
    char* diffuse_path;
    char* normal_path;
    AlphaMode alpha_mode;
} MaterialInfo;

// Camera setup calculated from model bounds
typedef struct CameraSetup {
    vec3 position;
    vec3 target;
    float model_scale;
} CameraSetup;

// Utility functions
static inline char* str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static inline float clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

#endif // DCAT_TYPES_H
