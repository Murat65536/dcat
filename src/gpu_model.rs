use std::ops::Range;

use bytemuck::{Pod, Zeroable};
use glam::{Mat4, Vec3};
use wgpu::{
    AddressMode, BindGroup, BindGroupDescriptor, BindGroupEntry, BindGroupLayout, BindingResource,
    Buffer, BufferUsages, Device, Extent3d, FilterMode, IndexFormat, Origin3d, Queue, RenderPass,
    RenderPipeline, SamplerDescriptor, TexelCopyBufferLayout, TexelCopyTextureInfo, TextureAspect,
    TextureDescriptor, TextureDimension, TextureFormat, TextureUsages, TextureView,
    TextureViewDescriptor,
    util::{BufferInitDescriptor, DeviceExt},
};

use crate::{
    model::{AlphaMode, Material, ModelData, ModelTexture},
    texture::TextureImage,
};

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct MaterialUniforms {
    base_color: [f32; 4],
    alpha_mode: u32,
    alpha_cutoff: f32,
    specular_strength: f32,
    shininess: f32,
    use_diffuse_alpha_as_luster: u32,
    _padding: [u32; 3],
}

impl MaterialUniforms {
    fn new(material: &Material, diffuse_has_transparency: bool) -> Self {
        Self {
            base_color: material.base_color,
            alpha_mode: match material.alpha_mode {
                AlphaMode::Opaque => 0,
                AlphaMode::Mask => 1,
                AlphaMode::Blend => 2,
            },
            alpha_cutoff: material.alpha_cutoff,
            specular_strength: material.specular_strength,
            shininess: material.shininess,
            // An opaque material's unused alpha channel often holds a gloss map
            use_diffuse_alpha_as_luster: u32::from(
                material.alpha_mode == AlphaMode::Opaque && diffuse_has_transparency,
            ),
            _padding: [0; 3],
        }
    }
}

struct GpuBatch {
    index_range: Range<u32>,
    base_vertex: i32,
    instance_range: Range<u32>,
    material: usize,
    blend: bool,
}

// A model's geometry, textures, and materials uploaded to the GPU
pub struct GpuModel {
    vertex_buffer: Buffer,
    index_buffer: Buffer,
    instance_buffer: Buffer,
    batches: Vec<GpuBatch>,
    // Model-space center of each instance, for ordering blended draws
    instance_centers: Vec<Vec3>,
    material_bind_groups: Vec<BindGroup>,
    has_uvs: bool,
}

impl GpuModel {
    pub fn new(
        device: &Device,
        queue: &Queue,
        model: &ModelData,
        material_layout: &BindGroupLayout,
    ) -> Self {
        let vertex_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Vertex Buffer"),
            contents: bytemuck::cast_slice(&model.vertices),
            usage: BufferUsages::VERTEX,
        });
        let index_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Index Buffer"),
            contents: bytemuck::cast_slice(&model.indices),
            usage: BufferUsages::INDEX,
        });
        let instance_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Model Instance Buffer"),
            contents: bytemuck::cast_slice(&model.instances),
            usage: BufferUsages::VERTEX,
        });

        let texture_views: Vec<TextureView> = model
            .textures
            .iter()
            .map(|texture| upload_texture(device, queue, texture))
            .collect();

        let sampler = device.create_sampler(&SamplerDescriptor {
            label: Some("Material Sampler"),
            address_mode_u: AddressMode::Repeat,
            address_mode_v: AddressMode::Repeat,
            address_mode_w: AddressMode::Repeat,
            mag_filter: FilterMode::Linear,
            min_filter: FilterMode::Linear,
            ..Default::default()
        });

        let material_bind_groups = model
            .materials
            .iter()
            .map(|material| {
                let has_transparency = model.textures[material.diffuse].image.has_transparency();
                let uniforms = MaterialUniforms::new(material, has_transparency);
                let uniform_buffer = device.create_buffer_init(&BufferInitDescriptor {
                    label: Some("Material Uniform Buffer"),
                    contents: bytemuck::bytes_of(&uniforms),
                    usage: BufferUsages::UNIFORM,
                });

                device.create_bind_group(&BindGroupDescriptor {
                    label: Some("Material Bind Group"),
                    layout: material_layout,
                    entries: &[
                        BindGroupEntry {
                            binding: 0,
                            resource: uniform_buffer.as_entire_binding(),
                        },
                        BindGroupEntry {
                            binding: 1,
                            resource: BindingResource::TextureView(
                                &texture_views[material.diffuse],
                            ),
                        },
                        BindGroupEntry {
                            binding: 2,
                            resource: BindingResource::TextureView(&texture_views[material.normal]),
                        },
                        BindGroupEntry {
                            binding: 3,
                            resource: BindingResource::Sampler(&sampler),
                        },
                    ],
                })
            })
            .collect();

        let batches = model
            .batches
            .iter()
            .map(|batch| GpuBatch {
                index_range: batch.index_range.clone(),
                base_vertex: batch.base_vertex,
                instance_range: batch.instance_range.clone(),
                material: batch.material,
                blend: model.materials[batch.material].alpha_mode == AlphaMode::Blend,
            })
            .collect();

        Self {
            vertex_buffer,
            index_buffer,
            instance_buffer,
            batches,
            instance_centers: model.instance_centers.clone(),
            material_bind_groups,
            has_uvs: model.has_uvs,
        }
    }

    pub fn has_uvs(&self) -> bool {
        self.has_uvs
    }

    // Draw opaque meshes first, then blended ones over them from back to front
    pub fn draw(
        &self,
        pass: &mut RenderPass,
        opaque_pipeline: &RenderPipeline,
        blend_pipeline: &RenderPipeline,
        model: Mat4,
        camera_position: Vec3,
    ) {
        pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
        pass.set_vertex_buffer(1, self.instance_buffer.slice(..));
        pass.set_index_buffer(self.index_buffer.slice(..), IndexFormat::Uint32);

        pass.set_pipeline(opaque_pipeline);
        for batch in self.batches.iter().filter(|batch| !batch.blend) {
            if batch.instance_range.is_empty() {
                continue;
            }
            pass.set_bind_group(1, &self.material_bind_groups[batch.material], &[]);
            pass.draw_indexed(
                batch.index_range.clone(),
                batch.base_vertex,
                batch.instance_range.clone(),
            );
        }

        let order = blend_draw_order(
            &self.batches,
            &self.instance_centers,
            model,
            camera_position,
        );
        if order.is_empty() {
            return;
        }
        pass.set_pipeline(blend_pipeline);
        let mut bound_material = None;
        for (batch_index, instance) in order {
            let batch = &self.batches[batch_index];
            if bound_material != Some(batch.material) {
                pass.set_bind_group(1, &self.material_bind_groups[batch.material], &[]);
                bound_material = Some(batch.material);
            }
            pass.draw_indexed(
                batch.index_range.clone(),
                batch.base_vertex,
                instance..instance + 1,
            );
        }
    }
}

// Blended instances as (batch, instance), farthest from the camera first.
// They are drawn without depth writes, so each must be drawn after whatever
// it covers.
fn blend_draw_order(
    batches: &[GpuBatch],
    instance_centers: &[Vec3],
    model: Mat4,
    camera_position: Vec3,
) -> Vec<(usize, u32)> {
    let mut draws: Vec<(f32, usize, u32)> = batches
        .iter()
        .enumerate()
        .filter(|(_, batch)| batch.blend)
        .flat_map(|(index, batch)| batch.instance_range.clone().map(move |i| (index, i)))
        .map(|(index, instance)| {
            let center = model.transform_point3(instance_centers[instance as usize]);
            (center.distance_squared(camera_position), index, instance)
        })
        .collect();
    draws.sort_by(|a, b| b.0.total_cmp(&a.0));
    draws
        .into_iter()
        .map(|(_, index, instance)| (index, instance))
        .collect()
}

fn upload_texture(device: &Device, queue: &Queue, texture: &ModelTexture) -> TextureView {
    let max_dimension = device.limits().max_texture_dimension_2d;
    let resized;
    let image: &TextureImage = if texture.image.width.max(texture.image.height) > max_dimension {
        resized = texture.image.clone().fit_within(max_dimension);
        &resized
    } else {
        &texture.image
    };

    let format = if texture.srgb {
        TextureFormat::Rgba8UnormSrgb
    } else {
        TextureFormat::Rgba8Unorm
    };
    let size = Extent3d {
        width: image.width,
        height: image.height,
        depth_or_array_layers: 1,
    };
    let gpu_texture = device.create_texture(&TextureDescriptor {
        label: Some("Material Texture"),
        size,
        mip_level_count: 1,
        sample_count: 1,
        dimension: TextureDimension::D2,
        format,
        usage: TextureUsages::TEXTURE_BINDING | TextureUsages::COPY_DST,
        view_formats: &[],
    });

    queue.write_texture(
        TexelCopyTextureInfo {
            texture: &gpu_texture,
            mip_level: 0,
            origin: Origin3d::ZERO,
            aspect: TextureAspect::All,
        },
        &image.pixels,
        TexelCopyBufferLayout {
            offset: 0,
            bytes_per_row: Some(image.width * 4),
            rows_per_image: Some(image.height),
        },
        size,
    );

    gpu_texture.create_view(&TextureViewDescriptor::default())
}

#[cfg(test)]
mod tests {
    use super::*;

    // Must match MaterialUniforms in shader.wgsl
    #[test]
    fn uniform_size_matches_shader() {
        assert_eq!(std::mem::size_of::<MaterialUniforms>(), 48);
    }

    fn batch(instance_range: Range<u32>, blend: bool) -> GpuBatch {
        GpuBatch {
            index_range: 0..3,
            base_vertex: 0,
            instance_range,
            material: 0,
            blend,
        }
    }

    #[test]
    fn blended_instances_draw_far_to_near() {
        // Opaque batch 0, blended batch 1 with two instances, blended batch 2
        let batches = [batch(0..1, false), batch(1..3, true), batch(3..4, true)];
        let centers = [
            Vec3::new(0.0, 0.0, -9.0),
            Vec3::new(0.0, 0.0, -2.0),
            Vec3::new(0.0, 0.0, -8.0),
            Vec3::new(0.0, 0.0, -5.0),
        ];
        let order = blend_draw_order(&batches, &centers, Mat4::IDENTITY, Vec3::ZERO);
        assert_eq!(order, vec![(1, 2), (2, 3), (1, 1)]);

        // The model transform moves the instances before measuring
        let flipped = Mat4::from_rotation_y(std::f32::consts::PI);
        let camera = Vec3::new(0.0, 0.0, 10.0);
        let order = blend_draw_order(&batches, &centers, flipped, camera);
        assert_eq!(order, vec![(1, 1), (2, 3), (1, 2)]);
    }
}
