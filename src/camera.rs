use std::f32::consts::FRAC_PI_2;

use asset_importer::AABB;
use glam::{Mat4, Vec3};

const FOV_DEGREES: f32 = 60.0;
const NEAR_PLANE: f32 = 0.01;
const FAR_PLANE: f32 = 100.0;
const MAX_PITCH: f32 = FRAC_PI_2 - 0.01;
const MIN_ORBIT_DISTANCE: f32 = 0.1;
// Zoom and pan move further per step the further the camera is from its target
const DISTANCE_SCALING_POWER: f32 = 1.25;

#[derive(Clone, Debug)]
pub struct Camera {
    pub position: Vec3,
    pub target: Vec3,
    fov: f32,
    aspect: f32,
    yaw: f32,
    pitch: f32,
}

impl Camera {
    pub fn new(position: Vec3, target: Vec3, width: u32, height: u32) -> Self {
        let direction = (target - position).normalize_or_zero();
        Self {
            position,
            target,
            fov: FOV_DEGREES.to_radians(),
            aspect: width as f32 / height as f32,
            yaw: direction.z.atan2(direction.x),
            pitch: direction.y.asin(),
        }
    }

    pub fn set_viewport(&mut self, width: u32, height: u32) {
        self.aspect = width as f32 / height as f32;
    }

    pub fn view_matrix(&self) -> Mat4 {
        Mat4::look_at_rh(self.position, self.target, Vec3::Y)
    }

    pub fn projection_matrix(&self) -> Mat4 {
        Mat4::perspective_rh(self.fov, self.aspect, NEAR_PLANE, FAR_PLANE)
    }

    // Unit vector the camera looks along, from its yaw and pitch
    pub fn forward(&self) -> Vec3 {
        Vec3::new(
            self.yaw.cos() * self.pitch.cos(),
            self.pitch.sin(),
            self.yaw.sin() * self.pitch.cos(),
        )
    }

    fn distance(&self) -> f32 {
        self.position.distance(self.target)
    }

    fn translate(&mut self, offset: Vec3) {
        self.position += offset;
        self.target += offset;
    }

    fn right(&self) -> Vec3 {
        (self.target - self.position)
            .normalize_or_zero()
            .cross(Vec3::Y)
            .normalize_or_zero()
    }

    // Move along the view direction projected onto the ground plane
    pub fn move_forward(&mut self, distance: f32) {
        let horizontal_forward = Vec3::Y.cross(self.right()).normalize_or_zero();
        self.translate(horizontal_forward * distance);
    }

    pub fn move_right(&mut self, distance: f32) {
        self.translate(self.right() * distance);
    }

    pub fn move_up(&mut self, distance: f32) {
        self.translate(Vec3::Y * distance);
    }

    // Turn the camera in place (first-person look)
    pub fn rotate(&mut self, yaw_delta: f32, pitch_delta: f32) {
        self.add_angles(yaw_delta, pitch_delta);

        let distance = match self.distance() {
            d if d < 0.001 => 1.0,
            d => d,
        };
        self.target = self.position + self.forward() * distance;
    }

    // Swing the camera around its target
    pub fn orbit(&mut self, yaw_delta: f32, pitch_delta: f32) {
        self.add_angles(yaw_delta, pitch_delta);
        self.set_orbit_distance(self.distance());
    }

    pub fn zoom(&mut self, delta: f32) {
        let distance = self.distance();
        let distance = distance - delta * distance.powf(DISTANCE_SCALING_POWER);
        self.set_orbit_distance(distance.max(MIN_ORBIT_DISTANCE));
    }

    // Slide the camera and target together in the view plane
    pub fn pan(&mut self, dx: f32, dy: f32) {
        let scale = self.distance().powf(DISTANCE_SCALING_POWER);
        let forward = (self.target - self.position).normalize_or_zero();
        let right = self.right();
        let up = right.cross(forward).normalize_or_zero();
        self.translate(right * (-dx * scale) + up * (dy * scale));
    }

    fn add_angles(&mut self, yaw_delta: f32, pitch_delta: f32) {
        self.yaw += yaw_delta;
        self.pitch = (self.pitch + pitch_delta).clamp(-MAX_PITCH, MAX_PITCH);
    }

    fn set_orbit_distance(&mut self, distance: f32) {
        self.position = self.target - self.forward() * distance;
    }
}

// Initial placement of the model and camera, derived from the model's bounds.
// The model is centered on the origin and scaled so its diagonal equals the
// requested model scale.
pub struct Framing {
    center: Vec3,
    diagonal: f32,
}

impl Framing {
    pub fn from_bounds(bounds: &AABB) -> Self {
        if bounds.is_empty() {
            return Self {
                center: Vec3::ZERO,
                diagonal: 0.0,
            };
        }

        let min = Vec3::from(bounds.min);
        let max = Vec3::from(bounds.max);
        Self {
            center: (min + max) * 0.5,
            diagonal: min.distance(max),
        }
    }

    fn scale_factor(&self, model_scale: f32) -> f32 {
        if self.diagonal > 0.0 {
            model_scale / self.diagonal
        } else {
            1.0
        }
    }

    pub fn model_matrix(&self, coordinate_system: Mat4, model_scale: f32) -> Mat4 {
        coordinate_system
            * Mat4::from_scale(Vec3::splat(self.scale_factor(model_scale)))
            * Mat4::from_translation(-self.center)
    }

    // Camera position looking at the origin, slightly above and to the side
    pub fn camera_position(&self, model_scale: f32, camera_distance: Option<f32>) -> Vec3 {
        let diagonal = if self.diagonal > 0.0 {
            self.diagonal
        } else {
            1.0
        };
        let offset = Vec3::new(diagonal * 0.3, diagonal * 0.2, diagonal * 1.2)
            * self.scale_factor(model_scale);

        match camera_distance {
            Some(distance) => offset.normalize() * distance,
            None => offset,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use asset_importer::Vector3D;

    const EPSILON: f32 = 1e-4;

    fn camera() -> Camera {
        Camera::new(Vec3::new(1.0, 2.0, 3.0), Vec3::ZERO, 80, 48)
    }

    #[test]
    fn forward_points_at_target() {
        let camera = camera();
        let expected = (camera.target - camera.position).normalize();
        assert!(camera.forward().abs_diff_eq(expected, EPSILON));
    }

    #[test]
    fn orbit_keeps_distance_and_target() {
        let mut camera = camera();
        let distance = camera.distance();
        camera.orbit(0.7, -0.3);
        assert!((camera.distance() - distance).abs() < EPSILON);
        assert_eq!(camera.target, Vec3::ZERO);
    }

    #[test]
    fn pitch_is_clamped() {
        let mut camera = camera();
        camera.orbit(0.0, 10.0);
        assert!((camera.pitch - MAX_PITCH).abs() < EPSILON);
        camera.rotate(0.0, -20.0);
        assert!((camera.pitch + MAX_PITCH).abs() < EPSILON);
    }

    #[test]
    fn zoom_moves_toward_target_and_stops_at_minimum() {
        let mut camera = camera();
        let distance = camera.distance();
        camera.zoom(0.05);
        assert!(camera.distance() < distance);
        for _ in 0..1000 {
            camera.zoom(0.5);
        }
        assert!((camera.distance() - MIN_ORBIT_DISTANCE).abs() < EPSILON);
    }

    #[test]
    fn rotate_keeps_position() {
        let mut camera = camera();
        let position = camera.position;
        camera.rotate(0.5, 0.2);
        assert_eq!(camera.position, position);
        assert!(
            camera
                .forward()
                .abs_diff_eq((camera.target - position).normalize(), EPSILON)
        );
    }

    #[test]
    fn movement_translates_camera_and_target_together() {
        let mut camera = camera();
        let offset = camera.target - camera.position;
        camera.move_forward(1.0);
        camera.move_right(0.5);
        camera.move_up(0.25);
        camera.pan(0.1, 0.2);
        assert!((camera.target - camera.position).abs_diff_eq(offset, EPSILON));
    }

    #[test]
    fn move_forward_stays_level() {
        let mut camera = camera();
        let height = camera.position.y;
        camera.move_forward(2.0);
        assert!((camera.position.y - height).abs() < EPSILON);
    }

    #[test]
    fn framing_normalizes_model_size() {
        let bounds = AABB::new(Vector3D::new(1.0, 1.0, 1.0), Vector3D::new(3.0, 5.0, 5.0));
        let framing = Framing::from_bounds(&bounds);
        let model = framing.model_matrix(Mat4::IDENTITY, 2.0);

        // Center maps to the origin and the diagonal to the requested scale
        assert!(
            model
                .transform_point3(Vec3::new(2.0, 3.0, 3.0))
                .abs_diff_eq(Vec3::ZERO, EPSILON)
        );
        let min = model.transform_point3(Vec3::new(1.0, 1.0, 1.0));
        let max = model.transform_point3(Vec3::new(3.0, 5.0, 5.0));
        assert!((min.distance(max) - 2.0).abs() < EPSILON);
    }

    #[test]
    fn camera_distance_override() {
        let bounds = AABB::new(
            Vector3D::new(-1.0, -1.0, -1.0),
            Vector3D::new(1.0, 1.0, 1.0),
        );
        let framing = Framing::from_bounds(&bounds);
        let default = framing.camera_position(1.0, None);
        let overridden = framing.camera_position(1.0, Some(5.0));
        assert!((overridden.length() - 5.0).abs() < EPSILON);
        assert!(
            overridden
                .normalize()
                .abs_diff_eq(default.normalize(), EPSILON)
        );
    }

    #[test]
    fn empty_bounds_are_usable() {
        let framing = Framing::from_bounds(&AABB::empty());
        assert!(framing.model_matrix(Mat4::IDENTITY, 1.0).is_finite());
        assert!(framing.camera_position(1.0, None).is_finite());
    }
}
