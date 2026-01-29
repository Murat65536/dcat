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

struct CameraSetup {
    glm::vec3 position;
    glm::vec3 target;
    float modelScale;
};

CameraSetup calculateCameraSetup(const std::vector<Vertex>& vertices);
bool loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, bool& outHasUVs);
