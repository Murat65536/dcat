use cgmath::{Deg, Matrix4, Point3, Rad, Vector3, perspective};
pub struct Camera {
    pub position: Point3<f32>,
    pub target: Point3<f32>,
    pub up: Vector3<f32>,
    pub fov: Rad<f32>,
    pub aspect: f32,
    pub near: f32,
    pub far: f32,
}

impl Camera {
    pub fn new(width: u32, height: u32, position: Point3<f32>, target: Point3<f32>, fov: Deg<f32>) -> Self {
        Camera {
            position,
            target,
            up: Vector3::new(0.0, 1.0, 0.0),
            fov: fov.into(),
            aspect: width as f32 / height as f32,
            near: 0.1,
            far: 100.0,
        }
    }

    pub fn view_matrix(&self) -> Matrix4<f32> {
        Matrix4::look_at_rh(self.position, self.target, self.up)
    }

    pub fn projection_matrix(&self) -> Matrix4<f32> {
        perspective(self.fov, self.aspect, self.near, self.far)
    }
}
