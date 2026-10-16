use std::{error::Error, ops::Range, path::Path};

use asset_importer::{AABB, Importer, Scene, node::Node, postprocess::PostProcessSteps};
use glam::Mat4;
use wgpu::{
    Buffer, BufferUsages, Device, IndexFormat, RenderPass,
    util::{BufferInitDescriptor, DeviceExt},
};

// Transform matrix for an instance of a mesh
pub type InstanceMatrix = [[f32; 4]; 4];

pub struct Model {
    vertex_buffer: Buffer,
    index_buffer: Buffer,
    instance_buffer: Buffer,
    batches: Vec<MeshBatch>,
    bounds: AABB,
}

struct MeshBatch {
    index_range: Range<u32>,
    base_vertex: i32,
    instance_range: Range<u32>,
}

impl Model {
    pub fn load(device: &Device, path: &Path) -> Result<Self, Box<dyn Error>> {
        // Get the 3d model
        let scene = Importer::new().import_file_with(path.as_os_str(), |b| {
            b.with_post_process(
                PostProcessSteps::TRIANGULATE
                    | PostProcessSteps::FLIP_UVS
                    | PostProcessSteps::GEN_BOUNDING_BOXES,
            )
        })?;

        let mesh_count = scene.num_meshes();
        let mut vertices = Vec::<u8>::new();
        let mut indices = Vec::<u32>::new();
        let mut batches = Vec::<MeshBatch>::new();
        let mut scene_to_gpu = vec![None; mesh_count];

        let mut vertex_count = 0;

        for (scene_index, gpu_slot) in scene_to_gpu.iter_mut().enumerate() {
            // Ignore empty meshes
            let Some(mesh) = scene.mesh(scene_index) else {
                continue;
            };

            // Ignore meshes without vertices
            if !mesh.has_vertices() {
                continue;
            }

            let mesh_indices = mesh.triangle_indices_iter().collect::<Vec<u32>>();

            // Ignore meshes without indices
            if mesh_indices.is_empty() {
                continue;
            }

            let first_index = u32::try_from(indices.len())?;
            let base_vertex = i32::try_from(vertex_count)?;

            // Append vertices and indices of each mesh
            vertices.extend_from_slice(mesh.vertices_bytes());
            indices.extend_from_slice(&mesh_indices);

            let last_index = u32::try_from(indices.len())?;
            let gpu_index = batches.len();

            batches.push(MeshBatch {
                index_range: first_index..last_index,
                base_vertex,
                instance_range: 0..0,
            });

            *gpu_slot = Some(gpu_index);
            vertex_count += mesh.num_vertices();
        }

        let mut instance_batches = vec![Vec::<InstanceMatrix>::new(); batches.len()];

        let mut bounds = AABB::empty();

        if let Some(root) = scene.root_node() {
            Self::visit_node(
                &scene,
                &root,
                Mat4::IDENTITY,
                &scene_to_gpu,
                &mut instance_batches,
                &mut bounds,
            );
        }

        let total_instances = instance_batches.iter().map(Vec::len).sum();

        let mut instances = Vec::<InstanceMatrix>::with_capacity(total_instances);

        for (batch, mesh_instances) in batches.iter_mut().zip(instance_batches) {
            let first_instance = u32::try_from(instances.len())?;

            instances.extend(mesh_instances);

            let last_instance = u32::try_from(instances.len())?;
            batch.instance_range = first_instance..last_instance;
        }

        let vertex_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Vertex Buffer"),
            contents: &vertices,
            usage: BufferUsages::VERTEX,
        });

        let index_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Index Buffer"),
            contents: bytemuck::cast_slice(&indices),
            usage: BufferUsages::INDEX,
        });

        let instance_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Instance Buffer"),
            contents: bytemuck::cast_slice(&instances),
            usage: BufferUsages::VERTEX | BufferUsages::COPY_DST,
        });

        Ok(Self {
            vertex_buffer,
            index_buffer,
            instance_buffer,
            batches,
            bounds,
        })
    }

    fn visit_node(
        scene: &Scene,
        node: &Node,
        parent_world: Mat4,
        scene_to_gpu: &[Option<usize>],
        instance_batches: &mut [Vec<InstanceMatrix>],
        bounds: &mut AABB,
    ) {
        let local: Mat4 = node.transformation().into();
        let world = parent_world * local;

        for scene_index in node.mesh_indices_iter() {
            let Some(gpu_index) = scene_to_gpu.get(scene_index).copied().flatten() else {
                continue;
            };

            instance_batches[gpu_index].push(world.to_cols_array_2d());

            if let Some(mesh) = scene.mesh(scene_index) {
                let importer_world = world.into();
                let world_bounds = mesh.aabb().transformed(&importer_world);

                bounds.expand_to_include_aabb(&world_bounds);
            }
        }

        for child in node.children() {
            Self::visit_node(scene, &child, world, scene_to_gpu, instance_batches, bounds);
        }
    }

    pub fn draw<'a>(&'a self, pass: &mut RenderPass<'a>) {
        pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
        pass.set_vertex_buffer(1, self.instance_buffer.slice(..));
        pass.set_index_buffer(self.index_buffer.slice(..), IndexFormat::Uint32);

        for batch in &self.batches {
            if batch.instance_range.is_empty() {
                continue;
            }

            pass.draw_indexed(
                batch.index_range.clone(),
                batch.base_vertex,
                batch.instance_range.clone(),
            );
        }
    }

    pub fn bounds(&self) -> AABB {
        self.bounds
    }
}
