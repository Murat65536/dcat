#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "animation.hpp"

struct Vertex {
    glm::vec3 position;
    glm::vec2 texcoord;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::ivec4 boneIDs = glm::ivec4(-1);      // 4 bone indices per vertex
    glm::vec4 boneWeights = glm::vec4(0.0f);  // 4 weights (must sum to 1.0)
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint64_t generation = 0; // Incremented when data changes

    // Animation data
    bool hasAnimations = false;
    Skeleton skeleton;
    std::vector<Animation> animations;
    
    // Coordinate system conversion (for handling Z-up models, etc.)
    glm::mat4 coordinateSystemTransform = glm::mat4(1.0f);
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
