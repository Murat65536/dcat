#include "skydome.hpp"
#include <cmath>

Mesh generateSkydome(float radius, int segments, int rings) {
    Mesh mesh;
    
    // Generate vertices
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = M_PI * float(ring) / float(rings);
        float y = radius * cos(phi);
        float ringRadius = radius * sin(phi);
        
        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * M_PI * float(seg) / float(segments);
            float x = ringRadius * cos(theta);
            float z = ringRadius * sin(theta);
            
            Vertex vertex;
            vertex.position = glm::vec3(x, y, z);
            
            // Inverted normals (pointing inward)
            vertex.normal = glm::normalize(glm::vec3(-x, -y, -z));
            
            // UV coordinates
            vertex.texcoord = glm::vec2(
                float(seg) / float(segments),
                float(ring) / float(rings)
            );
            
            // Default tangent space
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
            
            // No bones
            vertex.boneIDs = glm::ivec4(-1);
            vertex.boneWeights = glm::vec4(0.0f);
            
            mesh.vertices.push_back(vertex);
        }
    }
    
    // Generate indices (inverted winding for inside rendering)
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            int current = ring * (segments + 1) + seg;
            int next = current + segments + 1;
            
            // Triangle 1 (inverted winding)
            mesh.indices.push_back(current);
            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);
            
            // Triangle 2 (inverted winding)
            mesh.indices.push_back(next);
            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next + 1);
        }
    }
    
    mesh.hasAnimations = false;
    mesh.coordinateSystemTransform = glm::mat4(1.0f);
    mesh.generation = 1;
    
    return mesh;
}
