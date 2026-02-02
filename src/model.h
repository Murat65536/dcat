#ifndef DCAT_MODEL_H
#define DCAT_MODEL_H

#include "types.h"
#include "animation.h"

// Mesh structure
typedef struct Mesh {
    VertexArray vertices;
    Uint32Array indices;
    uint64_t generation;  // Incremented when data changes
    
    // Animation data
    bool has_animations;
    Skeleton skeleton;
    AnimationArray animations;
    
    // Coordinate system conversion (for handling Z-up models, etc.)
    mat4 coordinate_system_transform;
} Mesh;

// Initialize mesh to empty state
void mesh_init(Mesh* mesh);

// Free mesh resources
void mesh_free(Mesh* mesh);

// Calculate camera setup based on model bounds
void calculate_camera_setup(const VertexArray* vertices, CameraSetup* setup);

// Load model from file using assimp C API
bool load_model(const char* path, Mesh* mesh, bool* out_has_uvs, MaterialInfo* out_material);

// Material info functions
void material_info_init(MaterialInfo* info);
void material_info_free(MaterialInfo* info);

#endif // DCAT_MODEL_H
