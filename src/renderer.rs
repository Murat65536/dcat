use std::{error::Error, mem, path::Path, sync::mpsc::channel};

use glam::Mat4;
use wgpu::{
    BindGroup, BindGroupDescriptor, BindGroupEntry, Buffer, BufferDescriptor, BufferUsages,
    COPY_BYTES_PER_ROW_ALIGNMENT, Color, CommandEncoderDescriptor, CompareFunction, DepthBiasState,
    DepthStencilState, Device, DeviceDescriptor, Extent3d, FragmentState, Instance,
    InstanceDescriptor, LoadOp, MapMode, MultisampleState, Operations, Origin3d, PollType,
    PrimitiveState, Queue, RenderPassColorAttachment, RenderPassDepthStencilAttachment,
    RenderPassDescriptor, RenderPipeline, RenderPipelineDescriptor, RequestAdapterOptions,
    StencilState, StoreOp, TexelCopyBufferInfo, TexelCopyBufferLayout, TexelCopyTextureInfo,
    Texture, TextureAspect, TextureDescriptor, TextureDimension, TextureFormat, TextureUsages,
    TextureView, TextureViewDescriptor, VertexAttribute, VertexBufferLayout, VertexFormat,
    VertexState, VertexStepMode, include_wgsl,
    util::{BufferInitDescriptor, DeviceExt},
    vertex_attr_array,
};

use crate::model::{InstanceMatrix, Model};

pub struct Renderer {
    device: Device,
    queue: Queue,
    targets: Targets,
    pipeline: RenderPipeline,
    model: Model,
    camera_buffer: Buffer,
    camera_bind_group: BindGroup,
}

// Resources whose size follows the output resolution
struct Targets {
    render_texture: Texture,
    render_texture_view: TextureView,
    depth_texture_view: TextureView,
    output_staging_buffer: Buffer,
    width: u32,
    height: u32,
    padded_bytes_per_row: u32,
}

const COLOR_FORMAT: TextureFormat = TextureFormat::Rgba8UnormSrgb;
const DEPTH_FORMAT: TextureFormat = TextureFormat::Depth32Float;
const INSTANCE_ATTRIBUTES: [VertexAttribute; 4] = vertex_attr_array![
    1 => Float32x4,
    2 => Float32x4,
    3 => Float32x4,
    4 => Float32x4,
];

impl Renderer {
    pub async fn new(model_path: &Path, width: u32, height: u32) -> Result<Self, Box<dyn Error>> {
        // Acquire device
        let instance = Instance::new(InstanceDescriptor::new_without_display_handle());
        let adapter = instance
            .request_adapter(&RequestAdapterOptions::default())
            .await?;
        let (device, queue) = adapter.request_device(&DeviceDescriptor::default()).await?;

        let targets = Targets::new(&device, width, height);

        let shader = device.create_shader_module(include_wgsl!("shader.wgsl"));

        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("Pipeline"),
            layout: None,
            vertex: VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[
                    Some(VertexBufferLayout {
                        array_stride: 12,
                        step_mode: VertexStepMode::Vertex,
                        attributes: &[VertexAttribute {
                            format: VertexFormat::Float32x3,
                            offset: 0,
                            shader_location: 0,
                        }],
                    }),
                    Some(VertexBufferLayout {
                        array_stride: mem::size_of::<InstanceMatrix>() as u64,
                        step_mode: VertexStepMode::Instance,
                        attributes: &INSTANCE_ATTRIBUTES,
                    }),
                ],
            },
            fragment: Some(FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(COLOR_FORMAT.into())],
            }),
            primitive: PrimitiveState::default(),
            depth_stencil: Some(DepthStencilState {
                format: DEPTH_FORMAT,
                depth_write_enabled: Some(true),
                depth_compare: Some(CompareFunction::Less),
                stencil: StencilState::default(),
                bias: DepthBiasState::default(),
            }),
            multisample: MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });

        let model = Model::load(&device, model_path)?;

        let camera_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Camera Buffer"),
            contents: bytemuck::bytes_of(&Mat4::IDENTITY.to_cols_array_2d()),
            usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
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
            targets,
            pipeline,
            model,
            camera_buffer,
            camera_bind_group,
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if (width, height) != (self.targets.width, self.targets.height) {
            self.targets = Targets::new(&self.device, width, height);
        }
    }

    pub fn model(&self) -> &Model {
        &self.model
    }

    // Render with the given model-view-projection matrix and read the frame back as RGB
    pub fn render(&self, transform: Mat4) -> Result<Vec<u8>, Box<dyn Error>> {
        let targets = &self.targets;
        self.queue.write_buffer(
            &self.camera_buffer,
            0,
            bytemuck::bytes_of(&transform.to_cols_array_2d()),
        );

        let mut command_encoder = self
            .device
            .create_command_encoder(&CommandEncoderDescriptor::default());
        {
            let mut render_pass = command_encoder.begin_render_pass(&RenderPassDescriptor {
                label: Some("Render Pass"),
                color_attachments: &[Some(RenderPassColorAttachment {
                    view: &targets.render_texture_view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: Operations {
                        load: LoadOp::Clear(Color::GREEN),
                        store: StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                    view: &targets.depth_texture_view,
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
            render_pass.set_bind_group(0, &self.camera_bind_group, &[]);
            self.model.draw(&mut render_pass);
        }

        command_encoder.copy_texture_to_buffer(
            TexelCopyTextureInfo {
                texture: &targets.render_texture,
                mip_level: 0,
                origin: Origin3d::ZERO,
                aspect: TextureAspect::All,
            },
            TexelCopyBufferInfo {
                buffer: &targets.output_staging_buffer,
                layout: TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(targets.padded_bytes_per_row),
                    rows_per_image: Some(targets.height),
                },
            },
            Extent3d {
                width: targets.width,
                height: targets.height,
                depth_or_array_layers: 1,
            },
        );
        self.queue.submit(Some(command_encoder.finish()));

        let buffer_slice = targets.output_staging_buffer.slice(..);
        let (sender, receiver) = channel();
        buffer_slice.map_async(MapMode::Read, move |r| sender.send(r).unwrap());
        self.device.poll(PollType::wait_indefinitely())?;
        receiver.recv()??;
        let out = buffer_slice.get_mapped_range()?.to_vec();
        targets.output_staging_buffer.unmap();
        let rgba_row_bytes = targets.width as usize * 4;
        let padded_row_bytes = targets.padded_bytes_per_row as usize;
        let mut pixels = Vec::with_capacity(targets.width as usize * targets.height as usize * 3);
        for row in out.chunks_exact(padded_row_bytes) {
            for rgba in row[..rgba_row_bytes].as_chunks::<4>().0 {
                pixels.extend_from_slice(&rgba[..3]);
            }
        }
        Ok(pixels)
    }
}

impl Targets {
    fn new(device: &Device, width: u32, height: u32) -> Self {
        // Color screen
        let render_texture = device.create_texture(&TextureDescriptor {
            label: Some("Render Texture"),
            size: Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: COLOR_FORMAT,
            usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::COPY_SRC,
            view_formats: &[COLOR_FORMAT],
        });
        let render_texture_view = render_texture.create_view(&TextureViewDescriptor::default());

        // Depth screen
        let depth_texture = device.create_texture(&TextureDescriptor {
            label: Some("Depth Texture"),
            size: Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: DEPTH_FORMAT,
            usage: TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_texture_view = depth_texture.create_view(&TextureViewDescriptor::default());

        let padded_bytes_per_row = (width * 4).next_multiple_of(COPY_BYTES_PER_ROW_ALIGNMENT);
        let output_staging_buffer = device.create_buffer(&BufferDescriptor {
            label: Some("Output Staging Buffer"),
            size: (padded_bytes_per_row * height) as u64,
            usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        Self {
            render_texture,
            render_texture_view,
            depth_texture_view,
            output_staging_buffer,
            width,
            height,
            padded_bytes_per_row,
        }
    }
}
