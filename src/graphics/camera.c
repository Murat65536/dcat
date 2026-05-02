#include "camera.h"
#include "../core/types.h"
#include <cglm/cam.h>
#include <cglm/vec3.h>
#include <math.h>

static const float DISTANCE_SCALING_POWER = 1.25f;

void camera_init(Camera *cam, const uint32_t width, const uint32_t height, vec3 pos, vec3 tgt,
                 const float fov_degrees) {
    glm_vec3_copy(pos, cam->position);
    glm_vec3_copy(tgt, cam->target);
    glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, cam->up);
    cam->fov = glm_rad(fov_degrees);
    cam->aspect = (float)width / (float)height;
    cam->near_plane = 0.0001f;
    cam->far_plane = 100.0f;

    vec3 direction;
    glm_vec3_sub(cam->target, cam->position, direction);
    glm_vec3_normalize(direction);
    cam->yaw = atan2f(direction[2], direction[0]);
    cam->pitch = asinf(direction[1]);
}

void camera_view_matrix(const Camera *cam, mat4 out) {
    glm_lookat((float *)cam->position, (float *)cam->target, (float *)cam->up, out);
}

void camera_projection_matrix(const Camera *cam, mat4 out) {
    glm_perspective(cam->fov, cam->aspect, cam->near_plane, cam->far_plane, out);
    // Flip Y for Vulkan coordinate system
    out[1][1] *= -1.0f;
}

void camera_update_direction(Camera *cam) {
    float dist = glm_vec3_distance(cam->position, cam->target);
    if (dist < 0.001f) {
        dist = 1.0f;
    }

    vec3 direction = {cosf(cam->yaw) * cosf(cam->pitch), sinf(cam->pitch),
                      sinf(cam->yaw) * cosf(cam->pitch)};
    
    vec3 offset;
    glm_vec3_scale(direction, dist, offset);
    glm_vec3_add(cam->position, offset, cam->target);
}

void camera_move_forward(Camera *cam, const float distance) {
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

void camera_move_backward(Camera *cam, const float distance) {
    camera_move_forward(cam, -distance);
}

void camera_move_right(Camera *cam, const float distance) {
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

void camera_move_left(Camera *cam, const float distance) {
    camera_move_right(cam, -distance);
}

void camera_move_up(Camera *cam, const float distance) {
    cam->position[1] += distance;
    cam->target[1] += distance;
}

void camera_move_down(Camera *cam, const float distance) {
    camera_move_up(cam, -distance);
}

void camera_rotate(Camera *cam, const float yaw_delta, const float pitch_delta) {
    cam->yaw += yaw_delta;
    cam->pitch += pitch_delta;

    const float max_pitch = GLM_PI / 2.0f - 0.01f;
    cam->pitch = clampf(cam->pitch, -max_pitch, max_pitch);

    camera_update_direction(cam);
}

static void camera_set_orbit_position(Camera *cam, const float dist) {
    vec3 direction = {cosf(cam->yaw) * cosf(cam->pitch), sinf(cam->pitch),
                      sinf(cam->yaw) * cosf(cam->pitch)};

    vec3 offset;
    glm_vec3_scale(direction, dist, offset);
    glm_vec3_sub(cam->target, offset, cam->position);
}

void camera_orbit(Camera *cam, const float yaw_delta, const float pitch_delta) {
    cam->yaw += yaw_delta;
    cam->pitch += pitch_delta;

    const float max_pitch = GLM_PI / 2.0f - 0.01f;
    cam->pitch = clampf(cam->pitch, -max_pitch, max_pitch);

    camera_set_orbit_position(cam, glm_vec3_distance(cam->position, cam->target));
}

void camera_zoom(Camera *cam, const float delta) {
    float dist = glm_vec3_distance(cam->position, cam->target);
    dist -= delta * powf(dist, DISTANCE_SCALING_POWER);
    if (dist < 0.1f)
        dist = 0.1f;

    camera_set_orbit_position(cam, dist);
}

void camera_pan(Camera *cam, const float dx, const float dy) {
    const float dist = glm_vec3_distance(cam->position, cam->target);
    const float scale = powf(dist, DISTANCE_SCALING_POWER);
    vec3 forward, right, up;
    glm_vec3_sub(cam->target, cam->position, forward);
    glm_vec3_normalize(forward);

    glm_vec3_cross(forward, cam->up, right);
    glm_vec3_normalize(right);

    glm_vec3_cross(right, forward, up);
    glm_vec3_normalize(up);

    vec3 move_x, move_y, move;
    glm_vec3_scale(right, -dx * scale, move_x);
    glm_vec3_scale(up, dy * scale, move_y);
    glm_vec3_add(move_x, move_y, move);

    glm_vec3_add(cam->position, move, cam->position);
    glm_vec3_add(cam->target, move, cam->target);
}

void camera_forward_direction(const Camera *cam, vec3 out) {
    out[0] = cosf(cam->yaw) * cosf(cam->pitch);
    out[1] = sinf(cam->pitch);
    out[2] = sinf(cam->yaw) * cosf(cam->pitch);
}
