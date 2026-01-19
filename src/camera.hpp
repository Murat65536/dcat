#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;
    float fov;
    float aspect;
    float near;
    float far;
    float yaw;
    float pitch;

    Camera(uint32_t width, uint32_t height, glm::vec3 pos, glm::vec3 tgt, float fovDegrees);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const;
    void updateDirection();
    void moveForward(float distance);
    void moveBackward(float distance);
    void moveLeft(float distance);
    void moveRight(float distance);
    void moveUp(float distance);
    void moveDown(float distance);
    void rotate(float yawDelta, float pitchDelta);
    glm::vec3 forwardDirection() const;
};
