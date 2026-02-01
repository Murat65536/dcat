#include "animation.hpp"
#include "model.hpp"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <iostream>
#include <fstream>

static int findKeyIndex(const std::vector<VectorKey>& keys, float time) {
    for (size_t i = 0; i < keys.size() - 1; i++) {
        if (time < keys[i + 1].time) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(keys.size() - 1);
}

static int findRotationKeyIndex(const std::vector<QuaternionKey>& keys, float time) {
    for (size_t i = 0; i < keys.size() - 1; i++) {
        if (time < keys[i + 1].time) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(keys.size() - 1);
}

glm::vec3 interpolatePosition(const std::vector<VectorKey>& keys, float time) {
    if (keys.empty()) return glm::vec3(0.0f);
    if (keys.size() == 1) return keys[0].value;

    int index = findKeyIndex(keys, time);
    int nextIndex = index + 1;

    if (nextIndex >= static_cast<int>(keys.size())) {
        return keys[index].value;
    }

    float deltaTime = keys[nextIndex].time - keys[index].time;
    float factor = 0.0f;
    if (deltaTime > 0.00001f) {
        factor = (time - keys[index].time) / deltaTime;
    }
    factor = glm::clamp(factor, 0.0f, 1.0f);

    return glm::mix(keys[index].value, keys[nextIndex].value, factor);
}

glm::vec3 interpolateScale(const std::vector<VectorKey>& keys, float time) {
    if (keys.empty()) return glm::vec3(1.0f);
    if (keys.size() == 1) return keys[0].value;

    int index = findKeyIndex(keys, time);
    int nextIndex = index + 1;

    if (nextIndex >= static_cast<int>(keys.size())) {
        return keys[index].value;
    }

    float deltaTime = keys[nextIndex].time - keys[index].time;
    float factor = 0.0f;
    if (deltaTime > 0) {
        factor = (time - keys[index].time) / deltaTime;
    }
    factor = glm::clamp(factor, 0.0f, 1.0f);

    return glm::mix(keys[index].value, keys[nextIndex].value, factor);
}

glm::quat interpolateRotation(const std::vector<QuaternionKey>& keys, float time) {
    if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (keys.size() == 1) return keys[0].value;

    int index = findRotationKeyIndex(keys, time);
    int nextIndex = index + 1;

    if (nextIndex >= static_cast<int>(keys.size())) {
        return keys[index].value;
    }

    float deltaTime = keys[nextIndex].time - keys[index].time;
    float factor = 0.0f;
    if (deltaTime > 0.00001f) {
        factor = (time - keys[index].time) / deltaTime;
    }
    factor = glm::clamp(factor, 0.0f, 1.0f);

    glm::quat start = keys[index].value;
    glm::quat end = keys[nextIndex].value;

    if (glm::dot(start, end) < 0.0f) {
        end = -end;
    }

    return glm::normalize(glm::slerp(start, end, factor));
}

static void computeBoneTransform(const Skeleton& skeleton, const Animation& animation,
                                int boneIndex, float time, const glm::mat4& parentTransform,
                                glm::mat4* boneMatrices) {
    const BoneNode& node = skeleton.boneHierarchy[boneIndex];
    glm::mat4 nodeTransform = node.transformation;

    // Find bone animation for this bone
    const BoneAnimation* boneAnim = nullptr;
    for (const auto& anim : animation.boneAnimations) {
        if (anim.boneName == node.name) {
            boneAnim = &anim;
            break;
        }
    }

    if (boneAnim) {
        glm::vec3 position;
        if (boneAnim->positionKeys.empty()) position = node.initialPosition;
        else position = interpolatePosition(boneAnim->positionKeys, time);

        glm::quat rotation;
        if (boneAnim->rotationKeys.empty()) rotation = node.initialRotation;
        else rotation = interpolateRotation(boneAnim->rotationKeys, time);

        glm::vec3 scale;
        if (boneAnim->scaleKeys.empty()) scale = node.initialScale;
        else scale = interpolateScale(boneAnim->scaleKeys, time);

        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotationMatrix = glm::toMat4(rotation);
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

        nodeTransform = translationMatrix * rotationMatrix * scaleMatrix;
    }

    glm::mat4 globalTransform = parentTransform * nodeTransform;

    auto it = skeleton.boneMap.find(node.name);
    if (it != skeleton.boneMap.end()) {
        int boneIdx = it->second;
        if (boneIdx < MAX_BONES) {
            boneMatrices[boneIdx] = skeleton.globalInverseTransform *
                                   globalTransform *
                                   skeleton.bones[boneIdx].offsetMatrix;
        }
    }

    for (int childIdx : node.childIndices) {
        computeBoneTransform(skeleton, animation, childIdx, time, globalTransform, boneMatrices);
    }
}

void computeBoneMatrices(const Skeleton& skeleton, const Animation& animation,
                        float time, glm::mat4* boneMatrices) {
    if (skeleton.boneHierarchy.empty()) return;

    for (uint32_t i = 0; i < MAX_BONES; i++) {
        boneMatrices[i] = glm::mat4(1.0f);
    }

    for (size_t i = 0; i < skeleton.boneHierarchy.size(); i++) {
        if (skeleton.boneHierarchy[i].parentIndex == -1) {
            computeBoneTransform(skeleton, animation, static_cast<int>(i), time,
                               glm::mat4(1.0f), boneMatrices);
        }
    }
}

void updateAnimation(const Mesh& mesh, AnimationState& state, float deltaTime, glm::mat4* boneMatrices) {
    if (!mesh.hasAnimations || mesh.animations.empty()) {
        return;
    }

    if (state.currentAnimationIndex < 0 ||
        state.currentAnimationIndex >= static_cast<int>(mesh.animations.size())) {
        state.currentAnimationIndex = 0;
    }

    const Animation& animation = mesh.animations[state.currentAnimationIndex];
    float ticksPerSecond = animation.ticksPerSecond != 0.0f ? animation.ticksPerSecond : 25.0f;

    if (state.playing) {
        state.currentTime += deltaTime * ticksPerSecond;

        if (animation.duration > 0.0f && state.currentTime >= animation.duration) {
            state.currentTime = fmod(state.currentTime, animation.duration);
        }
    }

    computeBoneMatrices(mesh.skeleton, animation, state.currentTime, boneMatrices);
}