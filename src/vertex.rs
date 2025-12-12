use cgmath::{Vector2, Vector3};

#[derive(Clone, Copy, Debug)]
pub struct Vertex {
    pub position: Vector3<f32>,
    pub texcoord: Vector2<f32>,
    pub normal: Vector3<f32>,
    pub tangent: Vector3<f32>,
    pub bitangent: Vector3<f32>,
}
