#ifndef DCAT_MODEL_H
#define DCAT_MODEL_H

#include "../core/types.h"
#include "animation.h"

// Mesh structure with geometry, animation, and coordinate system data
typedef struct Mesh {
    VertexArray vertices;
    Uint32Array indices;
    uint64_t generation;
    
    bool has_animations;
    Skeleton skeleton;
    AnimationArray animations;
    
    mat4 coordinate_system_transform;
} Mesh;

// Initialize mesh to empty state
void mesh_init(Mesh* mesh);

// Free all mesh resources
void mesh_free(Mesh* mesh);

// Calculate optimal camera setup for viewing the model
void calculate_camera_setup(const VertexArray* vertices, CameraSetup* setup);

// Load 3D model from file using Assimp
bool load_model(const char* path, Mesh* mesh, bool* out_has_uvs, MaterialInfo* out_material);

// Material info management
void material_info_init(MaterialInfo* info);
void material_info_free(MaterialInfo* info);

#endif
