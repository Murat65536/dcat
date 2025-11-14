use crate::vertex::Vertex;
use cgmath::Vector3;
use russimp::scene::{PostProcess, Scene};
use std::path::Path;

pub fn calculate_camera_distance(vertices: &[Vertex]) -> f32 {
    if vertices.is_empty() {
        return 3.0;
    }

    let mut min = Vector3::new(f32::INFINITY, f32::INFINITY, f32::INFINITY);
    let mut max = Vector3::new(f32::NEG_INFINITY, f32::NEG_INFINITY, f32::NEG_INFINITY);

    for vertex in vertices {
        min.x = min.x.min(vertex.position.x);
        min.y = min.y.min(vertex.position.y);
        min.z = min.z.min(vertex.position.z);
        max.x = max.x.max(vertex.position.x);
        max.y = max.y.max(vertex.position.y);
        max.z = max.z.max(vertex.position.z);
    }

    let size = max - min;

    let diagonal = (size.x * size.x + size.y * size.y + size.z * size.z).sqrt();

    diagonal
}

pub fn load_obj(
    path: &Path,
) -> Result<(Vec<Vertex>, Vec<u32>), Box<dyn std::error::Error + Send + Sync>> {
    let scene = Scene::from_file(
        path.to_str().ok_or("Invalid path")?,
        vec![
            PostProcess::Triangulate,
            PostProcess::GenerateNormals,
            PostProcess::CalculateTangentSpace,
        ],
    )
    .map_err(|e| -> Box<dyn std::error::Error + Send + Sync> {
        Box::new(std::io::Error::other(e))
    })?;

    if scene.meshes.is_empty() {
        return Err("No meshes found in model file".into());
    }
    let mesh = &scene.meshes[0];

    let vertex_count = mesh.vertices.len();
    let mut vertices = Vec::with_capacity(vertex_count);

    for i in 0..vertex_count {
        let pos = &mesh.vertices[i];
        let x = pos.x;
        let y = pos.y;
        let z = pos.z;

        let (u, v) = if !mesh.texture_coords.is_empty() {
            if let Some(ref tex_coords) = mesh.texture_coords[0] {
                if i < tex_coords.len() {
                    let tex = &tex_coords[i];
                    (tex.x, 1.0 - tex.y)
                } else {
                    (0.0, 0.0)
                }
            } else {
                (0.0, 0.0)
            }
        } else {
            (0.0, 0.0)
        };

        let (nx, ny, nz) = if !mesh.normals.is_empty() {
            let n = &mesh.normals[i];
            (n.x, n.y, n.z)
        } else {
            (0.0, 1.0, 0.0)
        };

        let (tx, ty, tz) = if !mesh.tangents.is_empty() {
            let t = &mesh.tangents[i];
            (t.x, t.y, t.z)
        } else {
            (1.0, 0.0, 0.0)
        };

        let (bx, by, bz) = if !mesh.bitangents.is_empty() {
            let b = &mesh.bitangents[i];
            (b.x, b.y, b.z)
        } else {
            (0.0, 0.0, 1.0)
        };

        vertices.push(Vertex {
            position: Vector3::new(x, y, z),
            texcoord: cgmath::Vector2::new(u, v),
            normal: Vector3::new(nx, ny, nz),
            tangent: Vector3::new(tx, ty, tz),
            bitangent: Vector3::new(bx, by, bz),
        });
    }

    let mut indices = Vec::new();
    for face in &mesh.faces {
        indices.extend(&face.0);
    }

    Ok((vertices, indices))
}
