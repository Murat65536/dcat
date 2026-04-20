#include "skydome.h"
#include <math.h>
#include <string.h>

void generate_skydome(Mesh* mesh, float radius, int segments, int rings) {
    mesh_init(mesh);
    
    // Generate vertices
    for (int ring = 0; ring <= rings; ring++) {
        const float phi = GLM_PI * (float)ring / (float)rings;
        const float y = radius * cosf(phi);
        const float ring_radius = radius * sinf(phi);
        
        for (int seg = 0; seg <= segments; seg++) {
            const float theta = 2.0f * GLM_PI * (float)seg / (float)segments;
            const float x = ring_radius * cosf(theta);
            const float z = ring_radius * sinf(theta);
            
            Vertex vertex = {0};

            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.position[2] = z;
            
            // Inverted normals (pointing inward)
            const float len = sqrtf(x*x + y*y + z*z);
            if (len > 0.0001f) {
                vertex.normal[0] = -x / len;
                vertex.normal[1] = -y / len;
                vertex.normal[2] = -z / len;
            }
            
            // UV coordinates
            vertex.texcoord[0] = (float)seg / (float)segments;
            vertex.texcoord[1] = (float)ring / (float)rings;
            
            // Default tangent space
            vertex.tangent[0] = 1.0f;
            vertex.bitangent[1] = 1.0f;
            
            // No bones
            vertex.bone_ids[0] = -1;
            vertex.bone_ids[1] = -1;
            vertex.bone_ids[2] = -1;
            vertex.bone_ids[3] = -1;
            
            ARRAY_PUSH(mesh->vertices, vertex);
        }
    }
    
    // Generate indices (inverted winding for inside rendering)
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            const uint32_t current = ring * (segments + 1) + seg;
            const uint32_t next = current + segments + 1;
            
            // Triangle 1 (inverted winding)
            ARRAY_PUSH(mesh->indices, current);
            ARRAY_PUSH(mesh->indices, current + 1);
            ARRAY_PUSH(mesh->indices, next);
            
            // Triangle 2 (inverted winding)
            ARRAY_PUSH(mesh->indices, next);
            ARRAY_PUSH(mesh->indices, current + 1);
            ARRAY_PUSH(mesh->indices, next + 1);
        }
    }
    
    mesh->has_animations = false;
    glm_mat4_identity(mesh->coordinate_system_transform);
    mesh->generation = 1;
}
