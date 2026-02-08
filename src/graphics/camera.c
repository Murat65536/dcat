#include "camera.h"
#include "../core/types.h"
#include <math.h>
#include <cglm/cam.h>
#include <cglm/vec3.h>

void camera_init(Camera* cam, uint32_t width, uint32_t height,
                 vec3 pos, vec3 tgt, float fov_degrees) {
    glm_vec3_copy(pos, cam->position);
    glm_vec3_copy(tgt, cam->target);
    glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, cam->up);
    cam->fov = glm_rad(fov_degrees);
    cam->aspect = (float)width / (float)height;
    cam->near_plane = 0.01f;
    cam->far_plane = 100.0f;

    vec3 direction;
    glm_vec3_sub(cam->target, cam->position, direction);
    glm_vec3_normalize(direction);
    cam->yaw = atan2f(direction[2], direction[0]);
    cam->pitch = asinf(direction[1]);
}

void camera_view_matrix(const Camera* cam, mat4 out) {
    glm_lookat((float*)cam->position, (float*)cam->target, (float*)cam->up, out);
}

void camera_projection_matrix(const Camera* cam, mat4 out) {
    glm_perspective(cam->fov, cam->aspect, cam->near_plane, cam->far_plane, out);
    // Flip Y for Vulkan coordinate system
    out[1][1] *= -1.0f;
}

void camera_update_direction(Camera* cam) {
    vec3 direction = {
        cosf(cam->yaw) * cosf(cam->pitch),
        sinf(cam->pitch),
        sinf(cam->yaw) * cosf(cam->pitch)
    };
    glm_vec3_add(cam->position, direction, cam->target);
}

void camera_move_forward(Camera* cam, float distance) {
    vec3 forward, right, horizontal_forward;
    glm_vec3_sub(cam->target, cam->position, forward);
    glm_vec3_normalize(forward);
    glm_vec3_cross(forward, cam->up, right);
    glm_vec3_normalize(right);
    glm_vec3_cross(cam->up, right, horizontal_forward);
    glm_vec3_normalize(horizontal_forward);
    
    vec3 move;
    glm_vec3_scale(horizontal_forward, distance, move);
    glm_vec3_add(cam->position, move, cam->position);
    glm_vec3_add(cam->target, move, cam->target);
}

void camera_move_backward(Camera* cam, float distance) {
    vec3 forward, right, horizontal_forward;
    glm_vec3_sub(cam->target, cam->position, forward);
    glm_vec3_normalize(forward);
    glm_vec3_cross(forward, cam->up, right);
    glm_vec3_normalize(right);
    glm_vec3_cross(cam->up, right, horizontal_forward);
    glm_vec3_normalize(horizontal_forward);
    
    vec3 move;
    glm_vec3_scale(horizontal_forward, distance, move);
    glm_vec3_sub(cam->position, move, cam->position);
    glm_vec3_sub(cam->target, move, cam->target);
}

void camera_move_left(Camera* cam, float distance) {
    vec3 forward, right;
    glm_vec3_sub(cam->target, cam->position, forward);
    glm_vec3_normalize(forward);
    glm_vec3_cross(forward, cam->up, right);
    glm_vec3_normalize(right);
    
    vec3 move;
    glm_vec3_scale(right, distance, move);
    glm_vec3_sub(cam->position, move, cam->position);
    glm_vec3_sub(cam->target, move, cam->target);
}

void camera_move_right(Camera* cam, float distance) {
    vec3 forward, right;
    glm_vec3_sub(cam->target, cam->position, forward);
    glm_vec3_normalize(forward);
    glm_vec3_cross(forward, cam->up, right);
    glm_vec3_normalize(right);
    
    vec3 move;
    glm_vec3_scale(right, distance, move);
    glm_vec3_add(cam->position, move, cam->position);
    glm_vec3_add(cam->target, move, cam->target);
}

void camera_move_up(Camera* cam, float distance) {
    cam->position[1] += distance;
    cam->target[1] += distance;
}

void camera_move_down(Camera* cam, float distance) {
    cam->position[1] -= distance;
    cam->target[1] -= distance;
}

void camera_rotate(Camera* cam, float yaw_delta, float pitch_delta) {
    cam->yaw += yaw_delta;
    cam->pitch += pitch_delta;
    
    const float max_pitch = GLM_PI / 2.0f - 0.01f;
    cam->pitch = clampf(cam->pitch, -max_pitch, max_pitch);
    
    camera_update_direction(cam);
}

void camera_orbit(Camera* cam, float yaw_delta, float pitch_delta) {
    cam->yaw += yaw_delta;
    cam->pitch += pitch_delta;
    
    const float max_pitch = GLM_PI / 2.0f - 0.01f;
    cam->pitch = clampf(cam->pitch, -max_pitch, max_pitch);
    
    float dist = glm_vec3_distance(cam->position, cam->target);
    
    float cos_yaw = cosf(cam->yaw);
    float sin_yaw = sinf(cam->yaw);
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);
    
    vec3 direction = {
        cos_yaw * cos_pitch,
        sin_pitch,
        sin_yaw * cos_pitch
    };
    
    vec3 offset;
    glm_vec3_scale(direction, dist, offset);
    glm_vec3_sub(cam->target, offset, cam->position);
}

void camera_zoom(Camera* cam, float delta) {
    float dist = glm_vec3_distance(cam->position, cam->target);
    dist -= delta;
    if (dist < 0.1f) dist = 0.1f;
    
    float cos_yaw = cosf(cam->yaw);
    float sin_yaw = sinf(cam->yaw);
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);
    
    vec3 direction = {
        cos_yaw * cos_pitch,
        sin_pitch,
        sin_yaw * cos_pitch
    };
    
    vec3 offset;
    glm_vec3_scale(direction, dist, offset);
    glm_vec3_sub(cam->target, offset, cam->position);
}

void camera_forward_direction(const Camera* cam, vec3 out) {
    out[0] = cosf(cam->yaw) * cosf(cam->pitch);
    out[1] = sinf(cam->pitch);
    out[2] = sinf(cam->yaw) * cosf(cam->pitch);
}
