#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <map>

constexpr uint32_t MAX_BONES = 200;
constexpr uint32_t MAX_BONE_INFLUENCE = 4;

struct VectorKey {
    float time;
    glm::vec3 value;
};

struct QuaternionKey {
    float time;
    glm::quat value;
};

struct BoneAnimation {
    std::string boneName;
    std::vector<VectorKey> positionKeys;
    std::vector<VectorKey> scaleKeys;
    std::vector<QuaternionKey> rotationKeys;
};

struct Animation {
    std::string name;
    float duration;
    float ticksPerSecond;
    std::vector<BoneAnimation> boneAnimations;
};

struct BoneInfo {
    std::string name;
    glm::mat4 offsetMatrix;  // Mesh space to bone space transform
    int index;
};

struct BoneNode {
    std::string name;
    glm::mat4 transformation;
    glm::vec3 initialPosition;
    glm::quat initialRotation;
    glm::vec3 initialScale;
    int parentIndex;  // -1 for root
    std::vector<int> childIndices;
};

struct Skeleton {
    std::vector<BoneInfo> bones;
    std::vector<BoneNode> boneHierarchy;
    std::map<std::string, int> boneMap;  // name -> bone index
    glm::mat4 globalInverseTransform;
};

struct AnimationState {
    int currentAnimationIndex = 0;
    float currentTime = 0.0f;
    bool playing = true;
};

// Core animation functions
void updateAnimation(const struct Mesh& mesh, AnimationState& state, float deltaTime, glm::mat4* boneMatrices);

// Helper functions for interpolation
glm::vec3 interpolatePosition(const std::vector<VectorKey>& keys, float time);
glm::vec3 interpolateScale(const std::vector<VectorKey>& keys, float time);
glm::quat interpolateRotation(const std::vector<QuaternionKey>& keys, float time);

// Bone matrix computation
void computeBoneMatrices(const Skeleton& skeleton, const Animation& animation,
                        float time, glm::mat4* boneMatrices);
