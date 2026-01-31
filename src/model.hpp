#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

struct Vertex {
    glm::vec3 position;
    glm::vec2 texcoord;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint64_t generation = 0; // Incremented when data changes
};

struct CameraSetup {
    glm::vec3 position;
    glm::vec3 target;
    float modelScale;
};

enum class AlphaMode {
    Opaque,
    Mask,
    Blend
};

struct MaterialInfo {
    std::string diffusePath;
    std::string normalPath;
    AlphaMode alphaMode = AlphaMode::Opaque;
};

CameraSetup calculateCameraSetup(const std::vector<Vertex>& vertices);
bool loadModel(const std::string& path, Mesh& mesh, bool& outHasUVs, MaterialInfo& outMaterial);
