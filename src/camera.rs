use cgmath::{Deg, Matrix4, Point3, Rad, Vector3, perspective, InnerSpace};

pub struct Camera {
    pub position: Point3<f32>,
    pub target: Point3<f32>,
    pub up: Vector3<f32>,
    pub fov: Rad<f32>,
    pub aspect: f32,
    pub near: f32,
    pub far: f32,
    pub yaw: f32,
    pub pitch: f32,
}

impl Camera {
    pub fn new(width: u32, height: u32, position: Point3<f32>, target: Point3<f32>, fov: Deg<f32>) -> Self {
        let direction = (target - position).normalize();
        let yaw = direction.z.atan2(direction.x);
        let pitch = direction.y.asin();
        
        Camera {
            position,
            target,
            up: Vector3::new(0.0, 1.0, 0.0),
            fov: fov.into(),
            aspect: width as f32 / height as f32,
            near: 0.1,
            far: 100.0,
            yaw,
            pitch,
        }
    }

    pub fn view_matrix(&self) -> Matrix4<f32> {
        Matrix4::look_at_rh(self.position, self.target, self.up)
    }

    pub fn projection_matrix(&self) -> Matrix4<f32> {
        perspective(self.fov, self.aspect, self.near, self.far)
    }

    pub fn update_direction(&mut self) {
        let direction = Vector3::new(
            self.yaw.cos() * self.pitch.cos(),
            self.pitch.sin(),
            self.yaw.sin() * self.pitch.cos(),
        );
        self.target = self.position + direction;
    }

    pub fn move_forward(&mut self, distance: f32) {
        let forward = (self.target - self.position).normalize();
        self.position += forward * distance;
        self.target += forward * distance;
    }

    pub fn move_backward(&mut self, distance: f32) {
        let forward = (self.target - self.position).normalize();
        self.position -= forward * distance;
        self.target -= forward * distance;
    }

    pub fn move_left(&mut self, distance: f32) {
        let forward = (self.target - self.position).normalize();
        let right = forward.cross(self.up).normalize();
        self.position -= right * distance;
        self.target -= right * distance;
    }

    pub fn move_right(&mut self, distance: f32) {
        let forward = (self.target - self.position).normalize();
        let right = forward.cross(self.up).normalize();
        self.position += right * distance;
        self.target += right * distance;
    }

    pub fn move_up(&mut self, distance: f32) {
        self.position.y += distance;
        self.target.y += distance;
    }

    pub fn move_down(&mut self, distance: f32) {
        self.position.y -= distance;
        self.target.y -= distance;
    }

    pub fn rotate(&mut self, yaw_delta: f32, pitch_delta: f32) {
        self.yaw += yaw_delta;
        self.pitch += pitch_delta;
        
        let max_pitch = std::f32::consts::PI / 2.0 - 0.01;
        self.pitch = self.pitch.clamp(-max_pitch, max_pitch);
        
        self.update_direction();
    }
}
