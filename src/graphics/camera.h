#ifndef DCAT_CAMERA_H
#define DCAT_CAMERA_H

#include <stdint.h>
#include <cglm/types.h>

typedef struct Camera {
    vec3 position;
    vec3 target;
    vec3 up;
    float fov;
    float aspect;
    float near_plane;
    float far_plane;
    float yaw;
    float pitch;
} Camera;

// Initialize camera with position, target, and field of view
void camera_init(Camera* cam, uint32_t width, uint32_t height, vec3 pos, 
                 vec3 tgt, float fov_degrees);

// Generate view and projection matrices
void camera_view_matrix(const Camera* cam, mat4 out);
void camera_projection_matrix(const Camera* cam, mat4 out);

// Update camera direction from yaw/pitch angles
void camera_update_direction(Camera* cam);

// Camera movement in world space
void camera_move_forward(Camera* cam, float distance);
void camera_move_backward(Camera* cam, float distance);
void camera_move_left(Camera* cam, float distance);
void camera_move_right(Camera* cam, float distance);
void camera_move_up(Camera* cam, float distance);
void camera_move_down(Camera* cam, float distance);

// Camera rotation (FPS-style)
void camera_rotate(Camera* cam, float yaw_delta, float pitch_delta);

// Camera orbit around target
void camera_orbit(Camera* cam, float yaw_delta, float pitch_delta);
void camera_zoom(Camera* cam, float delta);

// Get camera forward direction vector
void camera_forward_direction(const Camera* cam, vec3 out);

#endif
