#ifndef DCAT_CAMERA_H
#define DCAT_CAMERA_H

#include "types.h"

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

void camera_init(Camera* cam, uint32_t width, uint32_t height,
                 vec3 pos, vec3 tgt, float fov_degrees);
void camera_view_matrix(const Camera* cam, mat4 out);
void camera_projection_matrix(const Camera* cam, mat4 out);
void camera_update_direction(Camera* cam);
void camera_move_forward(Camera* cam, float distance);
void camera_move_backward(Camera* cam, float distance);
void camera_move_left(Camera* cam, float distance);
void camera_move_right(Camera* cam, float distance);
void camera_move_up(Camera* cam, float distance);
void camera_move_down(Camera* cam, float distance);
void camera_rotate(Camera* cam, float yaw_delta, float pitch_delta);
void camera_orbit(Camera* cam, float yaw_delta, float pitch_delta);
void camera_zoom(Camera* cam, float delta);
void camera_forward_direction(const Camera* cam, vec3 out);

#endif // DCAT_CAMERA_H
