#ifndef DCAT_ANIMATION_H
#define DCAT_ANIMATION_H

#include "types.h"
#include <cglm/cglm.h>

// Animation key types
typedef struct VectorKey {
    float time;
    vec3 value;
} VectorKey;

typedef struct QuaternionKey {
    float time;
    versor value;  // cglm quaternion
} QuaternionKey;

// Dynamic arrays for keys
typedef struct VectorKeyArray {
    VectorKey* data;
    size_t count;
    size_t capacity;
} VectorKeyArray;

typedef struct QuaternionKeyArray {
    QuaternionKey* data;
    size_t count;
    size_t capacity;
} QuaternionKeyArray;

// Animation for a single bone
typedef struct BoneAnimation {
    char* bone_name;
    VectorKeyArray position_keys;
    VectorKeyArray scale_keys;
    QuaternionKeyArray rotation_keys;
} BoneAnimation;

typedef struct BoneAnimationArray {
    BoneAnimation* data;
    size_t count;
    size_t capacity;
} BoneAnimationArray;

// Complete animation
typedef struct Animation {
    char* name;
    float duration;
    float ticks_per_second;
    BoneAnimationArray bone_animations;
} Animation;

typedef struct AnimationArray {
    Animation* data;
    size_t count;
    size_t capacity;
} AnimationArray;

// Bone information
typedef struct BoneInfo {
    char* name;
    mat4 offset_matrix;  // Mesh space to bone space transform
    int index;
} BoneInfo;

typedef struct BoneInfoArray {
    BoneInfo* data;
    size_t count;
    size_t capacity;
} BoneInfoArray;

// Bone hierarchy node
typedef struct BoneNode {
    char* name;
    mat4 transformation;
    vec3 initial_position;
    versor initial_rotation;
    vec3 initial_scale;
    int parent_index;  // -1 for root
    IntArray child_indices;
} BoneNode;

typedef struct BoneNodeArray {
    BoneNode* data;
    size_t count;
    size_t capacity;
} BoneNodeArray;

// Bone name to index mapping using hash table for O(1) lookups
#define BONE_MAP_SIZE 256

static inline uint32_t bone_hash(const char* name) {
    uint32_t hash = 5381;
    while (*name) {
        hash = ((hash << 5) + hash) + (unsigned char)*name++;
    }
    return hash;
}

typedef struct BoneMapEntry {
    char* name;
    int index;
    struct BoneMapEntry* next;  // For collision chaining
} BoneMapEntry;

typedef struct BoneMap {
    BoneMapEntry* buckets[BONE_MAP_SIZE];
    BoneMapEntry* entries;  // Pool for all entries
    size_t count;
    size_t capacity;
} BoneMap;

// Skeleton
typedef struct Skeleton {
    BoneInfoArray bones;
    BoneNodeArray bone_hierarchy;
    BoneMap bone_map;
    mat4 global_inverse_transform;
} Skeleton;

// Animation state
typedef struct AnimationState {
    int current_animation_index;
    float current_time;
    bool playing;
} AnimationState;

// Forward declaration
struct Mesh;

// Initialize animation state
void animation_state_init(AnimationState* state);

// Core animation functions
void update_animation(const struct Mesh* mesh, AnimationState* state, float delta_time, mat4* bone_matrices);

// Helper functions for interpolation
void interpolate_position(const VectorKeyArray* keys, float time, vec3 out);
void interpolate_scale(const VectorKeyArray* keys, float time, vec3 out);
void interpolate_rotation(const QuaternionKeyArray* keys, float time, versor out);

// Bone matrix computation
void compute_bone_matrices(const Skeleton* skeleton, const Animation* animation,
                           float time, mat4* bone_matrices);

// Bone map functions
void bone_map_init(BoneMap* map);
void bone_map_free(BoneMap* map);
int bone_map_find(const BoneMap* map, const char* name);
void bone_map_insert(BoneMap* map, const char* name, int index);

// Cleanup functions
void skeleton_free(Skeleton* skeleton);
void animation_free(Animation* animation);
void animation_array_free(AnimationArray* arr);

#endif // DCAT_ANIMATION_H
