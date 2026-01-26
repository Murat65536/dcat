#include "camera.hpp"
#include <algorithm>
#include <cmath>

Camera::Camera(uint32_t width, uint32_t height, glm::vec3 pos, glm::vec3 tgt, float fovDegrees)
    : position(pos)
    , target(tgt)
    , up(0.0f, 1.0f, 0.0f)
    , fov(glm::radians(fovDegrees))
    , aspect(static_cast<float>(width) / static_cast<float>(height))
    , near(0.00001f)
    , far(100.0f)
{
    glm::vec3 direction = glm::normalize(target - position);
    yaw = std::atan2(direction.z, direction.x);
    pitch = std::asin(direction.y);
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::projectionMatrix() const {
    glm::mat4 proj = glm::perspective(fov, aspect, near, far);
    // Flip Y for Vulkan coordinate system
    proj[1][1] *= -1.0f;
    return proj;
}

void Camera::updateDirection() {
    glm::vec3 direction(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)
    );
    target = position + direction;
}

void Camera::moveForward(float distance) {
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 horizontalForward = glm::normalize(glm::cross(up, right));
    position += horizontalForward * distance;
    target += horizontalForward * distance;
}

void Camera::moveBackward(float distance) {
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 horizontalForward = glm::normalize(glm::cross(up, right));
    position -= horizontalForward * distance;
    target -= horizontalForward * distance;
}

void Camera::moveLeft(float distance) {
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    position -= right * distance;
    target -= right * distance;
}

void Camera::moveRight(float distance) {
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    position += right * distance;
    target += right * distance;
}

void Camera::moveUp(float distance) {
    position.y += distance;
    target.y += distance;
}

void Camera::moveDown(float distance) {
    position.y -= distance;
    target.y -= distance;
}

void Camera::rotate(float yawDelta, float pitchDelta) {
    yaw += yawDelta;
    pitch += pitchDelta;
    
    constexpr float maxPitch = 3.14159265f / 2.0f - 0.01f;
    pitch = std::clamp(pitch, -maxPitch, maxPitch);
    
    updateDirection();
}

glm::vec3 Camera::forwardDirection() const {
    return glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)
    );
}
