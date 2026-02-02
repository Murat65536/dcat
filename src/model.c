#include "model.h"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

void mesh_init(Mesh* mesh) {
    memset(mesh, 0, sizeof(Mesh));
    glm_mat4_identity(mesh->coordinate_system_transform);
}

void mesh_free(Mesh* mesh) {
    free(mesh->vertices.data);
    free(mesh->indices.data);
    skeleton_free(&mesh->skeleton);
    animation_array_free(&mesh->animations);
    mesh_init(mesh);
}

void material_info_init(MaterialInfo* info) {
    info->diffuse_path = NULL;
    info->normal_path = NULL;
    info->alpha_mode = ALPHA_MODE_OPAQUE;
}

void material_info_free(MaterialInfo* info) {
    free(info->diffuse_path);
    free(info->normal_path);
    material_info_init(info);
}

void calculate_camera_setup(const VertexArray* vertices, CameraSetup* setup) {
    if (vertices->count == 0) {
        glm_vec3_copy((vec3){0.0f, 0.0f, 3.0f}, setup->position);
        glm_vec3_zero(setup->target);
        setup->model_scale = 1.0f;
        return;
    }
    
    vec3 min_pos = {FLT_MAX, FLT_MAX, FLT_MAX};
    vec3 max_pos = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    
    for (size_t i = 0; i < vertices->count; i++) {
        const Vertex* v = &vertices->data[i];
        for (int j = 0; j < 3; j++) {
            if (v->position[j] < min_pos[j]) min_pos[j] = v->position[j];
            if (v->position[j] > max_pos[j]) max_pos[j] = v->position[j];
        }
    }
    
    vec3 center, size;
    glm_vec3_add(min_pos, max_pos, center);
    glm_vec3_scale(center, 0.5f, center);
    glm_vec3_sub(max_pos, min_pos, size);
    
    float diagonal = sqrtf(size[0]*size[0] + size[1]*size[1] + size[2]*size[2]);
    float distance = diagonal * 1.2f;
    
    vec3 camera_offset = {
        diagonal * 0.3f,
        diagonal * 0.2f,
        distance
    };
    
    glm_vec3_add(center, camera_offset, setup->position);
    glm_vec3_copy(center, setup->target);
    setup->model_scale = diagonal;
}

// Convert assimp matrix to cglm matrix
static void ai_matrix_to_glm(const struct aiMatrix4x4* from, mat4 to) {
    to[0][0] = from->a1; to[1][0] = from->a2; to[2][0] = from->a3; to[3][0] = from->a4;
    to[0][1] = from->b1; to[1][1] = from->b2; to[2][1] = from->b3; to[3][1] = from->b4;
    to[0][2] = from->c1; to[1][2] = from->c2; to[2][2] = from->c3; to[3][2] = from->c4;
    to[0][3] = from->d1; to[1][3] = from->d2; to[2][3] = from->d3; to[3][3] = from->d4;
}

static void ai_vector_to_glm(const struct aiVector3D* v, vec3 out) {
    out[0] = v->x;
    out[1] = v->y;
    out[2] = v->z;
}

static void ai_quat_to_glm(const struct aiQuaternion* q, versor out) {
    out[0] = q->x;
    out[1] = q->y;
    out[2] = q->z;
    out[3] = q->w;
}

static void process_node(const struct aiNode* node, const struct aiScene* scene, mat4 parent_transform,
                         VertexArray* vertices, Uint32Array* indices, bool* out_has_uvs) {
    mat4 node_transform, combined;
    ai_matrix_to_glm(&node->mTransformation, node_transform);
    glm_mat4_mul(parent_transform, node_transform, combined);
    
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        const struct aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        uint32_t base_index = (uint32_t)vertices->count;
        
        if (mesh->mTextureCoords[0]) {
            *out_has_uvs = true;
        }
        
        // Compute normal matrix
        mat3 normal_matrix;
        glm_mat4_pick3(combined, normal_matrix);
        glm_mat3_inv(normal_matrix, normal_matrix);
        glm_mat3_transpose(normal_matrix);
        
        // Process vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex;
            memset(&vertex, 0, sizeof(Vertex));
            vertex.bone_ids[0] = -1;
            vertex.bone_ids[1] = -1;
            vertex.bone_ids[2] = -1;
            vertex.bone_ids[3] = -1;
            
            // Apply transformation
            vec4 pos = {mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z, 1.0f};
            vec4 transformed;
            glm_mat4_mulv(combined, pos, transformed);
            glm_vec3_copy(transformed, vertex.position);
            
            if (mesh->mTextureCoords[0]) {
                vertex.texcoord[0] = mesh->mTextureCoords[0][j].x;
                vertex.texcoord[1] = 1.0f - mesh->mTextureCoords[0][j].y;
            }
            
            if (mesh->mNormals) {
                vec3 normal = {mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z};
                glm_mat3_mulv(normal_matrix, normal, vertex.normal);
                glm_vec3_normalize(vertex.normal);
            } else {
                glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, vertex.normal);
            }
            
            if (mesh->mTangents && mesh->mBitangents) {
                vec3 tangent = {mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z};
                vec3 bitangent = {mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z};
                glm_mat3_mulv(normal_matrix, tangent, vertex.tangent);
                glm_mat3_mulv(normal_matrix, bitangent, vertex.bitangent);
                glm_vec3_normalize(vertex.tangent);
                glm_vec3_normalize(vertex.bitangent);
            } else {
                glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f}, vertex.tangent);
                glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f}, vertex.bitangent);
            }
            
            ARRAY_PUSH(*vertices, vertex);
        }
        
        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            const struct aiFace* face = &mesh->mFaces[j];
            for (unsigned int k = 0; k < face->mNumIndices; k++) {
                uint32_t idx = base_index + face->mIndices[k];
                ARRAY_PUSH(*indices, idx);
            }
        }
    }
    
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        process_node(node->mChildren[i], scene, combined, vertices, indices, out_has_uvs);
    }
}

// Process node for animated models (no transform baking)
static void process_node_animated(const struct aiNode* node, const struct aiScene* scene,
                                  VertexArray* vertices, Uint32Array* indices,
                                  bool* out_has_uvs, Skeleton* skeleton) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        const struct aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        uint32_t base_index = (uint32_t)vertices->count;
        
        if (mesh->mTextureCoords[0]) {
            *out_has_uvs = true;
        }
        
        // Process vertices (no transformation - keep in bind pose)
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex;
            memset(&vertex, 0, sizeof(Vertex));
            vertex.bone_ids[0] = -1;
            vertex.bone_ids[1] = -1;
            vertex.bone_ids[2] = -1;
            vertex.bone_ids[3] = -1;
            
            vertex.position[0] = mesh->mVertices[j].x;
            vertex.position[1] = mesh->mVertices[j].y;
            vertex.position[2] = mesh->mVertices[j].z;
            
            if (mesh->mTextureCoords[0]) {
                vertex.texcoord[0] = mesh->mTextureCoords[0][j].x;
                vertex.texcoord[1] = 1.0f - mesh->mTextureCoords[0][j].y;
            }
            
            if (mesh->mNormals) {
                vertex.normal[0] = mesh->mNormals[j].x;
                vertex.normal[1] = mesh->mNormals[j].y;
                vertex.normal[2] = mesh->mNormals[j].z;
            } else {
                glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, vertex.normal);
            }
            
            if (mesh->mTangents && mesh->mBitangents) {
                vertex.tangent[0] = mesh->mTangents[j].x;
                vertex.tangent[1] = mesh->mTangents[j].y;
                vertex.tangent[2] = mesh->mTangents[j].z;
                vertex.bitangent[0] = mesh->mBitangents[j].x;
                vertex.bitangent[1] = mesh->mBitangents[j].y;
                vertex.bitangent[2] = mesh->mBitangents[j].z;
            } else {
                glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f}, vertex.tangent);
                glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f}, vertex.bitangent);
            }
            
            ARRAY_PUSH(*vertices, vertex);
        }
        
        // Extract bone weights
        if (mesh->mNumBones > 0) {
            for (unsigned int j = 0; j < mesh->mNumBones; j++) {
                const struct aiBone* bone = mesh->mBones[j];
                const char* bone_name = bone->mName.data;
                
                int bone_index = bone_map_find(&skeleton->bone_map, bone_name);
                if (bone_index < 0) {
                    bone_index = (int)skeleton->bones.count;
                    BoneInfo bone_info;
                    bone_info.name = str_dup(bone_name);
                    ai_matrix_to_glm(&bone->mOffsetMatrix, bone_info.offset_matrix);
                    bone_info.index = bone_index;
                    ARRAY_PUSH(skeleton->bones, bone_info);
                    bone_map_insert(&skeleton->bone_map, bone_name, bone_index);
                }
                
                // Assign bone weights to vertices
                for (unsigned int k = 0; k < bone->mNumWeights; k++) {
                    unsigned int vertex_id = bone->mWeights[k].mVertexId + base_index;
                    float weight = bone->mWeights[k].mWeight;
                    
                    if (vertex_id < vertices->count) {
                        Vertex* v = &vertices->data[vertex_id];
                        for (int slot = 0; slot < MAX_BONE_INFLUENCE; slot++) {
                            if (v->bone_ids[slot] < 0) {
                                v->bone_ids[slot] = bone_index;
                                v->bone_weights[slot] = weight;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Fix for vertices not influenced by any bone
        const char* node_name = node->mName.data;
        int node_bone_index = -1;
        
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            uint32_t global_vertex_idx = base_index + j;
            if (vertices->data[global_vertex_idx].bone_ids[0] < 0) {
                if (node_bone_index == -1) {
                    node_bone_index = bone_map_find(&skeleton->bone_map, node_name);
                    if (node_bone_index < 0) {
                        node_bone_index = (int)skeleton->bones.count;
                        BoneInfo bone_info;
                        bone_info.name = str_dup(node_name);
                        glm_mat4_identity(bone_info.offset_matrix);
                        bone_info.index = node_bone_index;
                        ARRAY_PUSH(skeleton->bones, bone_info);
                        bone_map_insert(&skeleton->bone_map, node_name, node_bone_index);
                    }
                }
                
                vertices->data[global_vertex_idx].bone_ids[0] = node_bone_index;
                vertices->data[global_vertex_idx].bone_weights[0] = 1.0f;
            }
        }
        
        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            const struct aiFace* face = &mesh->mFaces[j];
            for (unsigned int k = 0; k < face->mNumIndices; k++) {
                uint32_t idx = base_index + face->mIndices[k];
                ARRAY_PUSH(*indices, idx);
            }
        }
    }
    
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        process_node_animated(node->mChildren[i], scene, vertices, indices, out_has_uvs, skeleton);
    }
}

// Build bone hierarchy
// First pass: count total nodes
static int count_nodes(const struct aiNode* node) {
    int count = 1;
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        count += count_nodes(node->mChildren[i]);
    }
    return count;
}

// Build bone hierarchy iteratively to avoid reallocation issues
static void build_bone_hierarchy(const struct aiNode* root, Skeleton* skeleton, int parent_index) {
    (void)parent_index;  // Initial call uses -1
    
    // Pre-allocate to avoid reallocations during build
    int total_nodes = count_nodes(root);
    skeleton->bone_hierarchy.data = aligned_malloc(total_nodes * sizeof(BoneNode));
    skeleton->bone_hierarchy.capacity = total_nodes;
    skeleton->bone_hierarchy.count = 0;
    
    // Use a simple stack for iterative traversal
    typedef struct { const struct aiNode* node; int parent_idx; } StackItem;
    StackItem* stack = malloc(total_nodes * sizeof(StackItem));
    int stack_top = 0;
    
    stack[stack_top++] = (StackItem){root, -1};
    
    while (stack_top > 0) {
        StackItem item = stack[--stack_top];
        const struct aiNode* node = item.node;
        int parent_idx = item.parent_idx;
        
        BoneNode bone_node;
        memset(&bone_node, 0, sizeof(BoneNode));
        
        bone_node.name = str_dup(node->mName.data);
        ai_matrix_to_glm(&node->mTransformation, bone_node.transformation);
        
        // Decompose transform for fallback
        glm_vec3_copy((vec3){bone_node.transformation[3][0], bone_node.transformation[3][1], bone_node.transformation[3][2]}, 
                      bone_node.initial_position);
        
        // Scale
        bone_node.initial_scale[0] = glm_vec3_norm((vec3){bone_node.transformation[0][0], bone_node.transformation[0][1], bone_node.transformation[0][2]});
        bone_node.initial_scale[1] = glm_vec3_norm((vec3){bone_node.transformation[1][0], bone_node.transformation[1][1], bone_node.transformation[1][2]});
        bone_node.initial_scale[2] = glm_vec3_norm((vec3){bone_node.transformation[2][0], bone_node.transformation[2][1], bone_node.transformation[2][2]});
        
        // Rotation (from normalized matrix columns)
        if (bone_node.initial_scale[0] > 0.0001f && bone_node.initial_scale[1] > 0.0001f && bone_node.initial_scale[2] > 0.0001f) {
            mat3 rot_m;
            rot_m[0][0] = bone_node.transformation[0][0] / bone_node.initial_scale[0];
            rot_m[0][1] = bone_node.transformation[0][1] / bone_node.initial_scale[0];
            rot_m[0][2] = bone_node.transformation[0][2] / bone_node.initial_scale[0];
            rot_m[1][0] = bone_node.transformation[1][0] / bone_node.initial_scale[1];
            rot_m[1][1] = bone_node.transformation[1][1] / bone_node.initial_scale[1];
            rot_m[1][2] = bone_node.transformation[1][2] / bone_node.initial_scale[1];
            rot_m[2][0] = bone_node.transformation[2][0] / bone_node.initial_scale[2];
            rot_m[2][1] = bone_node.transformation[2][1] / bone_node.initial_scale[2];
            rot_m[2][2] = bone_node.transformation[2][2] / bone_node.initial_scale[2];
            glm_mat3_quat(rot_m, bone_node.initial_rotation);
        } else {
            glm_quat_identity(bone_node.initial_rotation);
        }
        
        bone_node.parent_index = parent_idx;
        ARRAY_INIT(bone_node.child_indices);
        
        int current_index = (int)skeleton->bone_hierarchy.count;
        skeleton->bone_hierarchy.data[skeleton->bone_hierarchy.count++] = bone_node;
        
        // Update parent's children list
        if (parent_idx >= 0) {
            ARRAY_PUSH(skeleton->bone_hierarchy.data[parent_idx].child_indices, current_index);
        }
        
        // Push children in reverse order so they're processed in order
        for (int i = (int)node->mNumChildren - 1; i >= 0; i--) {
            stack[stack_top++] = (StackItem){node->mChildren[i], current_index};
        }
    }
    
    free(stack);
}

static void load_animations(const struct aiScene* scene, AnimationArray* animations) {
    for (unsigned int i = 0; i < scene->mNumAnimations; i++) {
        const struct aiAnimation* ai_anim = scene->mAnimations[i];
        
        Animation animation;
        memset(&animation, 0, sizeof(Animation));
        animation.name = str_dup(ai_anim->mName.data);
        animation.duration = (float)ai_anim->mDuration;
        animation.ticks_per_second = (float)ai_anim->mTicksPerSecond;
        ARRAY_INIT(animation.bone_animations);
        
        for (unsigned int j = 0; j < ai_anim->mNumChannels; j++) {
            const struct aiNodeAnim* channel = ai_anim->mChannels[j];
            
            BoneAnimation bone_anim;
            memset(&bone_anim, 0, sizeof(BoneAnimation));
            bone_anim.bone_name = str_dup(channel->mNodeName.data);
            ARRAY_INIT(bone_anim.position_keys);
            ARRAY_INIT(bone_anim.scale_keys);
            ARRAY_INIT(bone_anim.rotation_keys);
            
            // Position keys
            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                VectorKey key;
                key.time = (float)channel->mPositionKeys[k].mTime;
                ai_vector_to_glm(&channel->mPositionKeys[k].mValue, key.value);
                ARRAY_PUSH(bone_anim.position_keys, key);
            }
            
            // Rotation keys
            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                QuaternionKey key;
                key.time = (float)channel->mRotationKeys[k].mTime;
                ai_quat_to_glm(&channel->mRotationKeys[k].mValue, key.value);
                ARRAY_PUSH(bone_anim.rotation_keys, key);
            }
            
            // Scale keys
            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                VectorKey key;
                key.time = (float)channel->mScalingKeys[k].mTime;
                ai_vector_to_glm(&channel->mScalingKeys[k].mValue, key.value);
                ARRAY_PUSH(bone_anim.scale_keys, key);
            }
            
            ARRAY_PUSH(animation.bone_animations, bone_anim);
        }
        
        // Calculate actual duration from keyframes
        float actual_duration = 0.0f;
        for (size_t j = 0; j < animation.bone_animations.count; j++) {
            const BoneAnimation* ba = &animation.bone_animations.data[j];
            if (ba->position_keys.count > 0) {
                float t = ba->position_keys.data[ba->position_keys.count - 1].time;
                if (t > actual_duration) actual_duration = t;
            }
            if (ba->rotation_keys.count > 0) {
                float t = ba->rotation_keys.data[ba->rotation_keys.count - 1].time;
                if (t > actual_duration) actual_duration = t;
            }
            if (ba->scale_keys.count > 0) {
                float t = ba->scale_keys.data[ba->scale_keys.count - 1].time;
                if (t > actual_duration) actual_duration = t;
            }
        }
        
        if (actual_duration > 0.0f && actual_duration < animation.duration) {
            animation.duration = actual_duration;
        }
        
        ARRAY_PUSH(*animations, animation);
    }
}

static char* resolve_texture_path(const char* model_path, const char* texture_path, const struct aiScene* scene) {
    if (!texture_path || !texture_path[0]) return NULL;
    
    // Check if this is an embedded texture reference
    if (texture_path[0] == '*') {
        return str_dup(texture_path);
    }
    
    // Try to match against embedded texture filenames
    if (scene && scene->mNumTextures > 0) {
        for (unsigned int i = 0; i < scene->mNumTextures; i++) {
            const struct aiTexture* tex = scene->mTextures[i];
            if (tex->mFilename.length > 0) {
                const char* embedded_filename = tex->mFilename.data;
                if (strcmp(embedded_filename, texture_path) == 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "*%u", i);
                    return str_dup(buf);
                }
                
                // Compare just filenames
                const char* slash1 = strrchr(embedded_filename, '/');
                const char* slash2 = strrchr(embedded_filename, '\\');
                const char* embedded_name = embedded_filename;
                if (slash1 && slash1 > embedded_name) embedded_name = slash1 + 1;
                if (slash2 && slash2 > embedded_name) embedded_name = slash2 + 1;
                
                slash1 = strrchr(texture_path, '/');
                slash2 = strrchr(texture_path, '\\');
                const char* requested_name = texture_path;
                if (slash1 && slash1 > requested_name) requested_name = slash1 + 1;
                if (slash2 && slash2 > requested_name) requested_name = slash2 + 1;
                
                if (strcmp(embedded_name, requested_name) == 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "*%u", i);
                    return str_dup(buf);
                }
            }
        }
    }
    
    char* clean_path = str_dup(texture_path);
    
    // Handle Windows absolute paths
    if (strlen(clean_path) >= 3 && clean_path[1] == ':' && (clean_path[2] == '\\' || clean_path[2] == '/')) {
        const char* last_sep = strrchr(clean_path, '\\');
        const char* last_fslash = strrchr(clean_path, '/');
        if (last_fslash > last_sep) last_sep = last_fslash;
        if (last_sep) {
            char* filename = str_dup(last_sep + 1);
            free(clean_path);
            clean_path = filename;
        }
    }
    
    // If absolute Unix path, return as-is
    if (clean_path[0] == '/') return clean_path;
    
    // Make relative to model directory
    const char* last_slash = strrchr(model_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - model_path + 1;
        size_t path_len = strlen(clean_path);
        char* full_path = malloc(dir_len + path_len + 1);
        memcpy(full_path, model_path, dir_len);
        memcpy(full_path + dir_len, clean_path, path_len + 1);
        free(clean_path);
        return full_path;
    }
    
    return clean_path;
}

bool load_model(const char* path, Mesh* mesh, bool* out_has_uvs, MaterialInfo* out_material) {
    const struct aiScene* scene = aiImportFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType
    );
    
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        fprintf(stderr, "Error loading model: %s\n", aiGetErrorString());
        return false;
    }
    
    // Check coordinate system metadata
    int up_axis = 1;  // Default Y-up
    int up_axis_sign = 1;
    
    if (scene->mMetaData) {
        const struct aiMetadata* meta = scene->mMetaData;
        for (unsigned int i = 0; i < meta->mNumProperties; i++) {
            if (strcmp(meta->mKeys[i].data, "UpAxis") == 0 && 
                meta->mValues[i].mType == AI_INT32) {
                up_axis = *(int*)meta->mValues[i].mData;
            } else if (strcmp(meta->mKeys[i].data, "UpAxisSign") == 0 &&
                       meta->mValues[i].mType == AI_INT32) {
                up_axis_sign = *(int*)meta->mValues[i].mData;
            }
        }
    }
    
    mat4 coordinate_conversion;
    glm_mat4_identity(coordinate_conversion);
    
    if (up_axis == 2 && up_axis_sign == 1) {
        // Z-up to Y-up
        glm_rotate_make(coordinate_conversion, glm_rad(-90.0f), (vec3){1.0f, 0.0f, 0.0f});
    } else if (up_axis == 0 && up_axis_sign == 1) {
        // X-up to Y-up
        glm_rotate_make(coordinate_conversion, glm_rad(90.0f), (vec3){0.0f, 0.0f, 1.0f});
    }
    
    *out_has_uvs = false;
    mesh_free(mesh);
    mesh_init(mesh);
    mesh->generation = 1;
    glm_mat4_copy(coordinate_conversion, mesh->coordinate_system_transform);
    material_info_init(out_material);
    
    // Check if model has animations
    mesh->has_animations = (scene->mNumAnimations > 0);
    
    if (mesh->has_animations) {
        // Process animated model
        bone_map_init(&mesh->skeleton.bone_map);
        ARRAY_INIT(mesh->skeleton.bones);
        ARRAY_INIT(mesh->skeleton.bone_hierarchy);
        
        process_node_animated(scene->mRootNode, scene, &mesh->vertices, &mesh->indices,
                             out_has_uvs, &mesh->skeleton);
        build_bone_hierarchy(scene->mRootNode, &mesh->skeleton, -1);
        load_animations(scene, &mesh->animations);
        
        mat4 root_transform;
        ai_matrix_to_glm(&scene->mRootNode->mTransformation, root_transform);
        glm_mat4_inv(root_transform, mesh->skeleton.global_inverse_transform);
    } else {
        mat4 identity;
        glm_mat4_identity(identity);
        process_node(scene->mRootNode, scene, identity, &mesh->vertices, &mesh->indices, out_has_uvs);
    }
    
    // Extract material info
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const struct aiMaterial* material = scene->mMaterials[i];
        struct aiString str;
        
        if (!out_material->diffuse_path) {
            if (aiGetMaterialTexture(material, aiTextureType_DIFFUSE, 0, &str, NULL, NULL, NULL, NULL, NULL, NULL) == aiReturn_SUCCESS) {
                out_material->diffuse_path = resolve_texture_path(path, str.data, scene);
            } else if (aiGetMaterialTexture(material, aiTextureType_BASE_COLOR, 0, &str, NULL, NULL, NULL, NULL, NULL, NULL) == aiReturn_SUCCESS) {
                out_material->diffuse_path = resolve_texture_path(path, str.data, scene);
            }
        }
        
        if (!out_material->normal_path) {
            if (aiGetMaterialTexture(material, aiTextureType_NORMALS, 0, &str, NULL, NULL, NULL, NULL, NULL, NULL) == aiReturn_SUCCESS) {
                out_material->normal_path = resolve_texture_path(path, str.data, scene);
            } else if (aiGetMaterialTexture(material, aiTextureType_HEIGHT, 0, &str, NULL, NULL, NULL, NULL, NULL, NULL) == aiReturn_SUCCESS) {
                out_material->normal_path = resolve_texture_path(path, str.data, scene);
            }
        }
        
        // Check alpha mode (GLTF specific - AI_MATKEY_GLTF_ALPHAMODE)
        struct aiString alpha_mode_str;
        if (aiGetMaterialString(material, "$mat.gltf.alphaMode", 0, 0, &alpha_mode_str) == aiReturn_SUCCESS) {
            if (strcmp(alpha_mode_str.data, "MASK") == 0) {
                out_material->alpha_mode = ALPHA_MODE_MASK;
            } else if (strcmp(alpha_mode_str.data, "BLEND") == 0) {
                out_material->alpha_mode = ALPHA_MODE_BLEND;
            }
        }
        
        if (out_material->diffuse_path && out_material->normal_path) {
            break;
        }
    }
    
    aiReleaseImport(scene);
    
    return mesh->vertices.count > 0;
}
