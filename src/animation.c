#include "animation.h"
#include "model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void animation_state_init(AnimationState* state) {
    state->current_animation_index = 0;
    state->current_time = 0.0f;
    state->playing = true;
}

void bone_map_init(BoneMap* map) {
    memset(map->buckets, 0, sizeof(map->buckets));
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

void bone_map_free(BoneMap* map) {
    for (size_t i = 0; i < map->count; i++) {
        free(map->entries[i].name);
    }
    free(map->entries);
    bone_map_init(map);
}

int bone_map_find(const BoneMap* map, const char* name) {
    uint32_t hash = bone_hash(name) % BONE_MAP_SIZE;
    BoneMapEntry* entry = map->buckets[hash];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->index;
        }
        entry = entry->next;
    }
    return -1;
}

void bone_map_insert(BoneMap* map, const char* name, int index) {
    if (map->count >= map->capacity) {
        map->capacity = map->capacity ? map->capacity * 2 : 32;
        map->entries = realloc(map->entries, map->capacity * sizeof(BoneMapEntry));
    }
    BoneMapEntry* entry = &map->entries[map->count];
    entry->name = str_dup(name);
    entry->index = index;
    
    uint32_t hash = bone_hash(name) % BONE_MAP_SIZE;
    entry->next = map->buckets[hash];
    map->buckets[hash] = entry;
    map->count++;
}

static int find_key_index(const VectorKeyArray* keys, float time) {
    if (keys->count == 0) return -1;
    if (keys->count == 1 || time <= keys->data[0].time) return 0;
    if (time >= keys->data[keys->count - 1].time) return (int)keys->count - 1;
    
    int left = 0;
    int right = (int)keys->count - 1;
    
    while (left < right - 1) {
        int mid = (left + right) / 2;
        if (keys->data[mid].time <= time) {
            left = mid;
        } else {
            right = mid;
        }
    }
    
    return left;
}

static int find_rotation_key_index(const QuaternionKeyArray* keys, float time) {
    if (keys->count == 0) return -1;
    if (keys->count == 1 || time <= keys->data[0].time) return 0;
    if (time >= keys->data[keys->count - 1].time) return (int)keys->count - 1;
    
    int left = 0;
    int right = (int)keys->count - 1;
    
    while (left < right - 1) {
        int mid = (left + right) / 2;
        if (keys->data[mid].time <= time) {
            left = mid;
        } else {
            right = mid;
        }
    }
    
    return left;
}

void interpolate_position(const VectorKeyArray* keys, float time, vec3 out) {
    if (keys->count == 0) {
        glm_vec3_zero(out);
        return;
    }
    if (keys->count == 1) {
        glm_vec3_copy(keys->data[0].value, out);
        return;
    }
    
    int index = find_key_index(keys, time);
    int next_index = index + 1;
    
    if (next_index >= (int)keys->count) {
        glm_vec3_copy(keys->data[index].value, out);
        return;
    }
    
    float delta_time = keys->data[next_index].time - keys->data[index].time;
    float factor = 0.0f;
    if (delta_time > 0.00001f) {
        factor = (time - keys->data[index].time) / delta_time;
    }
    factor = clampf(factor, 0.0f, 1.0f);
    
    glm_vec3_lerp(keys->data[index].value, keys->data[next_index].value, factor, out);
}

void interpolate_scale(const VectorKeyArray* keys, float time, vec3 out) {
    if (keys->count == 0) {
        glm_vec3_one(out);
        return;
    }
    if (keys->count == 1) {
        glm_vec3_copy(keys->data[0].value, out);
        return;
    }
    
    int index = find_key_index(keys, time);
    int next_index = index + 1;
    
    if (next_index >= (int)keys->count) {
        glm_vec3_copy(keys->data[index].value, out);
        return;
    }
    
    float delta_time = keys->data[next_index].time - keys->data[index].time;
    float factor = 0.0f;
    if (delta_time > 0.0f) {
        factor = (time - keys->data[index].time) / delta_time;
    }
    factor = clampf(factor, 0.0f, 1.0f);
    
    glm_vec3_lerp(keys->data[index].value, keys->data[next_index].value, factor, out);
}

void interpolate_rotation(const QuaternionKeyArray* keys, float time, versor out) {
    if (keys->count == 0) {
        glm_quat_identity(out);
        return;
    }
    if (keys->count == 1) {
        glm_quat_copy(keys->data[0].value, out);
        return;
    }
    
    int index = find_rotation_key_index(keys, time);
    int next_index = index + 1;
    
    if (next_index >= (int)keys->count) {
        glm_quat_copy(keys->data[index].value, out);
        return;
    }
    
    float delta_time = keys->data[next_index].time - keys->data[index].time;
    float factor = 0.0f;
    if (delta_time > 0.00001f) {
        factor = (time - keys->data[index].time) / delta_time;
    }
    factor = clampf(factor, 0.0f, 1.0f);
    
    versor start, end;
    glm_quat_copy(keys->data[index].value, start);
    glm_quat_copy(keys->data[next_index].value, end);
    
    if (glm_quat_dot(start, end) < 0.0f) {
        glm_vec4_negate(end);
    }
    
    glm_quat_slerp(start, end, factor, out);
    glm_quat_normalize(out);
}

static void compute_bone_transform(const Skeleton* skeleton, const Animation* animation,
                                   int bone_index, float time, mat4 parent_transform,
                                   mat4* bone_matrices) {
    const BoneNode* node = &skeleton->bone_hierarchy.data[bone_index];
    mat4 node_transform;
    glm_mat4_copy((vec4*)node->transformation, node_transform);

    const BoneAnimation* bone_anim = NULL;
    for (size_t i = 0; i < animation->bone_animations.count; i++) {
        if (strcmp(animation->bone_animations.data[i].bone_name, node->name) == 0) {
            bone_anim = &animation->bone_animations.data[i];
            break;
        }
    }
    
    if (bone_anim) {
        vec3 position, scale;
        versor rotation;
        
        if (bone_anim->position_keys.count == 0) {
            glm_vec3_copy((float*)node->initial_position, position);
        } else {
            interpolate_position(&bone_anim->position_keys, time, position);
        }
        
        if (bone_anim->rotation_keys.count == 0) {
            glm_quat_copy((float*)node->initial_rotation, rotation);
        } else {
            interpolate_rotation(&bone_anim->rotation_keys, time, rotation);
        }
        
        if (bone_anim->scale_keys.count == 0) {
            glm_vec3_copy((float*)node->initial_scale, scale);
        } else {
            interpolate_scale(&bone_anim->scale_keys, time, scale);
        }
        
        mat4 translation_matrix, rotation_matrix, scale_matrix;
        glm_translate_make(translation_matrix, position);
        glm_quat_mat4(rotation, rotation_matrix);
        glm_scale_make(scale_matrix, scale);
        
        glm_mat4_mul(translation_matrix, rotation_matrix, node_transform);
        glm_mat4_mul(node_transform, scale_matrix, node_transform);
    }
    
    mat4 global_transform;
    glm_mat4_mul(parent_transform, node_transform, global_transform);
    
    int bone_idx = bone_map_find(&skeleton->bone_map, node->name);
    if (bone_idx >= 0 && bone_idx < MAX_BONES) {
        mat4 temp;
        glm_mat4_mul((vec4*)skeleton->global_inverse_transform, global_transform, temp);
        glm_mat4_mul(temp, skeleton->bones.data[bone_idx].offset_matrix, bone_matrices[bone_idx]);
    }
    
    for (size_t i = 0; i < node->child_indices.count; i++) {
        compute_bone_transform(skeleton, animation, node->child_indices.data[i],
                              time, global_transform, bone_matrices);
    }
}

void compute_bone_matrices(const Skeleton* skeleton, const Animation* animation,
                           float time, mat4* bone_matrices) {
    if (skeleton->bone_hierarchy.count == 0) return;
    
    for (uint32_t i = 0; i < MAX_BONES; i++) {
        glm_mat4_identity(bone_matrices[i]);
    }
    
    for (size_t i = 0; i < skeleton->bone_hierarchy.count; i++) {
        if (skeleton->bone_hierarchy.data[i].parent_index == -1) {
            mat4 identity;
            glm_mat4_identity(identity);
            compute_bone_transform(skeleton, animation, (int)i, time, identity, bone_matrices);
        }
    }
}

void update_animation(const Mesh* mesh, AnimationState* state, float delta_time, mat4* bone_matrices) {
    if (!mesh->has_animations || mesh->animations.count == 0) {
        return;
    }
    
    if (state->current_animation_index < 0 ||
        state->current_animation_index >= (int)mesh->animations.count) {
        state->current_animation_index = 0;
    }
    
    const Animation* animation = &mesh->animations.data[state->current_animation_index];
    float ticks_per_second = animation->ticks_per_second != 0.0f ? animation->ticks_per_second : 25.0f;
    
    if (state->playing) {
        state->current_time += delta_time * ticks_per_second;
        
        if (animation->duration > 0.0f && state->current_time >= animation->duration) {
            state->current_time = fmodf(state->current_time, animation->duration);
        }
    }
    
    compute_bone_matrices(&mesh->skeleton, animation, state->current_time, bone_matrices);
}

void skeleton_free(Skeleton* skeleton) {
    for (size_t i = 0; i < skeleton->bones.count; i++) {
        free(skeleton->bones.data[i].name);
    }
    free(skeleton->bones.data);
    
    for (size_t i = 0; i < skeleton->bone_hierarchy.count; i++) {
        free(skeleton->bone_hierarchy.data[i].name);
        free(skeleton->bone_hierarchy.data[i].child_indices.data);
    }
    free(skeleton->bone_hierarchy.data);
    
    bone_map_free(&skeleton->bone_map);
    
    memset(skeleton, 0, sizeof(Skeleton));
}

void animation_free(Animation* animation) {
    free(animation->name);
    for (size_t i = 0; i < animation->bone_animations.count; i++) {
        free(animation->bone_animations.data[i].bone_name);
        free(animation->bone_animations.data[i].position_keys.data);
        free(animation->bone_animations.data[i].scale_keys.data);
        free(animation->bone_animations.data[i].rotation_keys.data);
    }
    free(animation->bone_animations.data);
}

void animation_array_free(AnimationArray* arr) {
    for (size_t i = 0; i < arr->count; i++) {
        animation_free(&arr->data[i]);
    }
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}
