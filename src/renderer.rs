use std::{error::Error, sync::mpsc::channel};

use asset_importer::mesh::Mesh;
use glam::{Mat4, Vec3};
use wgpu::{
    BindGroup, BindGroupDescriptor, BindGroupEntry, Buffer, BufferDescriptor, BufferUsages, Color,
    CommandEncoderDescriptor, DepthBiasState, DepthStencilState, Device, DeviceDescriptor,
    Extent3d, FragmentState, IndexFormat, Instance, InstanceDescriptor, LoadOp, MapMode,
    MultisampleState, Operations, Origin3d, PollType, PrimitiveState, Queue,
    RenderPassColorAttachment, RenderPassDepthStencilAttachment, RenderPassDescriptor,
    RenderPipeline, RenderPipelineDescriptor, RequestAdapterOptions, StencilState, StoreOp,
    TexelCopyBufferInfo, TexelCopyBufferLayout, TexelCopyTextureInfo, Texture, TextureAspect,
    TextureDescriptor, TextureDimension, TextureFormat, TextureUsages, TextureView,
    TextureViewDescriptor, VertexAttribute, VertexBufferLayout, VertexFormat, VertexState,
    VertexStepMode, include_wgsl,
    util::{BufferInitDescriptor, DeviceExt},
};
pub struct Renderer {
    device: Device,
    queue: Queue,
    render_target: Texture,
    render_target_view: TextureView,
    depth_texture_view: TextureView,
    output_staging_buffer: Buffer,
    pipeline: RenderPipeline,
    vertex_buffer: Buffer,
    index_buffer: Buffer,
    index_count: u32,
    camera_bind_group: BindGroup,
}

const WIDTH: u64 = 64;
const HEIGHT: u64 = 64;
const VIEW_MARGIN: f32 = 0.1;

impl Renderer {
    pub async fn new(mesh: &Mesh) -> Result<Self, Box<dyn Error>> {
        let instance = Instance::new(InstanceDescriptor::new_without_display_handle());
        let adapter = instance
            .request_adapter(&RequestAdapterOptions::default())
            .await?;
        let (device, queue) = adapter.request_device(&DeviceDescriptor::default()).await?;

        let shader = device.create_shader_module(include_wgsl!("shader.wgsl"));

        let render_target = device.create_texture(&TextureDescriptor {
            label: Some("Render Target"),
            size: Extent3d {
                width: WIDTH as u32,
                height: HEIGHT as u32,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: TextureFormat::Rgba8UnormSrgb,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::COPY_SRC,
            view_formats: &[TextureFormat::Rgba8UnormSrgb],
        });
        let render_target_view = render_target.create_view(&TextureViewDescriptor::default());

        let depth_texture = device.create_texture(&TextureDescriptor {
            label: Some("Depth Texture"),
            size: Extent3d {
                width: WIDTH as u32,
                height: HEIGHT as u32,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: TextureFormat::Depth32Float,
            usage: TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_texture_view = depth_texture.create_view(&TextureViewDescriptor::default());

        let output_staging_buffer = device.create_buffer(&BufferDescriptor {
            label: Some("Output Staging Buffer"),
            size: WIDTH * HEIGHT * 4,
            usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        let vertex_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Vertex Buffer"),
            contents: mesh.vertices_bytes(),
            usage: BufferUsages::VERTEX,
        });

        let indices = &mesh.triangle_indices_iter().collect::<Vec<u32>>();
        let index_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Index Buffer"),
            contents: bytemuck::cast_slice(indices),
            usage: BufferUsages::INDEX,
        });

        let transform_matrix = Self::generate_matrix(mesh).to_cols_array_2d();
        let camera_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Camera Buffer"),
            contents: bytemuck::bytes_of(&transform_matrix),
            usage: BufferUsages::UNIFORM,
        });

        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Pipeline"),
            layout: None,
            vertex: VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(VertexBufferLayout {
                    array_stride: 12,
                    step_mode: VertexStepMode::Vertex,
                    attributes: &[VertexAttribute {
                        format: VertexFormat::Float32x3,
                        offset: 0,
                        shader_location: 0,
                    }],
                })],
            },
            fragment: Some(FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(TextureFormat::Rgba8UnormSrgb.into())],
            }),
            primitive: PrimitiveState::default(),
            depth_stencil: Some(DepthStencilState {
                format: TextureFormat::Depth32Float,
                depth_write_enabled: Some(true),
                depth_compare: Some(wgpu::CompareFunction::Less),
                stencil: StencilState::default(),
                bias: DepthBiasState::default(),
            }),
            multisample: MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });

        let camera_bind_group = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Camera Bind Group"),
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[BindGroupEntry {
                binding: 0,
                resource: camera_buffer.as_entire_binding(),
            }],
        });

        Ok(Self {
            device,
            queue,
            render_target,
            render_target_view,
            depth_texture_view,
            output_staging_buffer,
            pipeline,
            vertex_buffer,
            index_buffer,
            index_count: indices.len() as u32,
            camera_bind_group,
        })
    }

    fn generate_matrix(mesh: &Mesh) -> Mat4 {
        let aabb = mesh.aabb();
        let center: Vec3 = aabb.center().into();
        let translation_matrix = Mat4::from_translation(-center);
        let size: Vec3 = aabb.size().into();
        let largest = size.max_element();
        let scale = if largest > 0.0 {
            2.0 * (1.0 - VIEW_MARGIN) / largest
        } else {
            1.0
        };
        let scale_matrix = Mat4::from_scale(Vec3::splat(scale));

        let projection_matrix =
            Mat4::perspective_rh(45f32.to_radians(), WIDTH as f32 / HEIGHT as f32, 0.1, 100.0);

        let view_matrix = Mat4::look_at_rh(Vec3::new(0.0, 0.0, 3.0), Vec3::ZERO, Vec3::Y);

        projection_matrix * view_matrix * scale_matrix * translation_matrix
    }

    pub fn render(&self) -> Result<Vec<u8>, Box<dyn Error>> {
        let mut command_encoder = self
            .device
            .create_command_encoder(&CommandEncoderDescriptor::default());
        {
            let mut render_pass = command_encoder.begin_render_pass(&RenderPassDescriptor {
                label: None,
                color_attachments: &[Some(RenderPassColorAttachment {
                    view: &self.render_target_view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: Operations {
                        load: LoadOp::Clear(Color::GREEN),
                        store: StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                    view: &self.depth_texture_view,
                    depth_ops: Some(Operations {
                        load: LoadOp::Clear(1.0),
                        store: StoreOp::Discard,
                    }),
                    stencil_ops: None,
                }),
                occlusion_query_set: None,
                timestamp_writes: None,
                multiview_mask: None,
            });
            render_pass.set_pipeline(&self.pipeline);
            render_pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
            render_pass.set_index_buffer(self.index_buffer.slice(..), IndexFormat::Uint32);
            render_pass.set_bind_group(0, &self.camera_bind_group, &[]);
            render_pass.draw_indexed(0..self.index_count, 0, 0..1);
        }

        command_encoder.copy_texture_to_buffer(
            TexelCopyTextureInfo {
                texture: &self.render_target,
                mip_level: 0,
                origin: Origin3d::ZERO,
                aspect: TextureAspect::All,
            },
            TexelCopyBufferInfo {
                buffer: &self.output_staging_buffer,
                layout: TexelCopyBufferLayout {
                    offset: 0,
                    // This needs to be a multiple of 256. Normally we would need to pad it but here we
                    // know it will work out.
                    bytes_per_row: Some(WIDTH as u32 * 4),
                    rows_per_image: Some(HEIGHT as u32),
                },
            },
            Extent3d {
                width: WIDTH as u32,
                height: HEIGHT as u32,
                depth_or_array_layers: 1,
            },
        );
        self.queue.submit(Some(command_encoder.finish()));

        let buffer_slice = self.output_staging_buffer.slice(..);
        let (sender, receiver) = channel();
        buffer_slice.map_async(MapMode::Read, move |r| sender.send(r).unwrap());
        self.device.poll(PollType::wait_indefinitely())?;
        receiver.recv()??;
        let pixels = buffer_slice.get_mapped_range()?.to_vec();
        self.output_staging_buffer.unmap();
        Ok(pixels)
    }
}
