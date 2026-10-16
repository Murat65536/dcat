use std::{error::Error, mem, sync::mpsc::channel};

use bytemuck::{Pod, Zeroable};
use glam::{Mat4, Vec3};
use wgpu::{
    BindGroup, BindGroupDescriptor, BindGroupEntry, BindGroupLayoutDescriptor,
    BindGroupLayoutEntry, BindingType, BlendComponent, BlendFactor, BlendOperation, BlendState,
    Buffer, BufferBindingType, BufferDescriptor, BufferUsages, COPY_BYTES_PER_ROW_ALIGNMENT, Color,
    ColorTargetState, ColorWrites, CommandEncoderDescriptor, CompareFunction, DepthBiasState,
    DepthStencilState, Device, DeviceDescriptor, Extent3d, Features, FragmentState, Instance,
    InstanceDescriptor, LoadOp, MapMode, MultisampleState, Operations, Origin3d, PipelineLayout,
    PipelineLayoutDescriptor, PollType, PolygonMode, PrimitiveState, Queue,
    RenderPassColorAttachment, RenderPassDepthStencilAttachment, RenderPassDescriptor,
    RenderPipeline, RenderPipelineDescriptor, RequestAdapterOptions, SamplerBindingType,
    ShaderModule, ShaderStages, StencilState, StoreOp, TexelCopyBufferInfo, TexelCopyBufferLayout,
    TexelCopyTextureInfo, Texture, TextureAspect, TextureDescriptor, TextureDimension,
    TextureFormat, TextureSampleType, TextureUsages, TextureView, TextureViewDescriptor,
    TextureViewDimension, VertexAttribute, VertexBufferLayout, VertexState, VertexStepMode,
    include_wgsl,
    util::{BufferInitDescriptor, DeviceExt},
    vertex_attr_array,
};

use crate::{
    gpu_model::GpuModel,
    model::{self, ModelData},
};

// The shader applies gamma itself, so the target stores its output as is
const COLOR_FORMAT: TextureFormat = TextureFormat::Rgba8Unorm;
const DEPTH_FORMAT: TextureFormat = TextureFormat::Depth32Float;

const VERTEX_ATTRIBUTES: [VertexAttribute; 5] = vertex_attr_array![
    0 => Float32x3,
    1 => Float32x2,
    2 => Float32x3,
    3 => Float32x3,
    4 => Float32x3,
];
const INSTANCE_ATTRIBUTES: [VertexAttribute; 7] = vertex_attr_array![
    5 => Float32x4,
    6 => Float32x4,
    7 => Float32x4,
    8 => Float32x4,
    9 => Float32x3,
    10 => Float32x3,
    11 => Float32x3,
];

// Ambient light from above and below
const HEMISPHERE_SKY_COLOR: [f32; 4] = [0.50, 0.50, 0.52, 0.0];
const HEMISPHERE_GROUND_COLOR: [f32; 4] = [0.18, 0.16, 0.14, 0.0];
const FILL_LIGHT_INTENSITY: f32 = 0.18;
const RIM_LIGHT_INTENSITY: f32 = 0.22;

// Per-frame scene parameters
pub struct FrameParams {
    pub view_projection: Mat4,
    pub model: Mat4,
    pub camera_position: Vec3,
    // Direction towards the key light
    pub light_direction: Vec3,
    pub lighting: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct FrameUniforms {
    view_projection: [[f32; 4]; 4],
    model: [[f32; 4]; 4],
    camera_position: [f32; 3],
    enable_lighting: u32,
    light_direction: [f32; 3],
    use_triplanar_mapping: u32,
    hemisphere_sky_color: [f32; 4],
    hemisphere_ground_color: [f32; 4],
    fill_light: [f32; 4],
    rim_light: [f32; 4],
}

impl FrameUniforms {
    fn new(params: &FrameParams, use_triplanar_mapping: bool) -> Self {
        let light = params.light_direction.normalize_or(Vec3::Y);
        // Fill and rim lights follow the key light, so lighting stays balanced
        // as the camera moves
        let fill = Vec3::new(-light.x, -light.y - 0.2, -light.z).normalize_or(Vec3::NEG_Y);
        let rim = -light;

        Self {
            view_projection: params.view_projection.to_cols_array_2d(),
            model: params.model.to_cols_array_2d(),
            camera_position: params.camera_position.to_array(),
            enable_lighting: u32::from(params.lighting),
            light_direction: light.to_array(),
            use_triplanar_mapping: u32::from(use_triplanar_mapping),
            hemisphere_sky_color: HEMISPHERE_SKY_COLOR,
            hemisphere_ground_color: HEMISPHERE_GROUND_COLOR,
            fill_light: fill.extend(FILL_LIGHT_INTENSITY).to_array(),
            rim_light: rim.extend(RIM_LIGHT_INTENSITY).to_array(),
        }
    }
}

pub struct Renderer {
    device: Device,
    queue: Queue,
    targets: Targets,
    opaque_pipeline: RenderPipeline,
    blend_pipeline: RenderPipeline,
    // None when the GPU can't draw lines
    wireframe_pipeline: Option<RenderPipeline>,
    wireframe: bool,
    model: GpuModel,
    frame_buffer: Buffer,
    frame_bind_group: BindGroup,
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

impl Renderer {
    pub async fn new(model: &ModelData, width: u32, height: u32) -> Result<Self, Box<dyn Error>> {
        let instance = Instance::new(InstanceDescriptor::new_without_display_handle());
        let adapter = instance
            .request_adapter(&RequestAdapterOptions::default())
            .await?;
        let (device, queue) = adapter
            .request_device(&DeviceDescriptor {
                label: Some("Device"),
                required_features: adapter.features() & Features::POLYGON_MODE_LINE,
                ..Default::default()
            })
            .await?;

        let targets = Targets::new(&device, width, height);

        let frame_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Frame Bind Group Layout"),
            entries: &[uniform_entry(0, ShaderStages::VERTEX_FRAGMENT)],
        });
        let material_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("Material Bind Group Layout"),
            entries: &[
                uniform_entry(0, ShaderStages::FRAGMENT),
                texture_entry(1),
                texture_entry(2),
                BindGroupLayoutEntry {
                    binding: 3,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Sampler(SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });
        let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
            label: Some("Pipeline Layout"),
            bind_group_layouts: &[Some(&frame_layout), Some(&material_layout)],
            ..Default::default()
        });

        let shader = device.create_shader_module(include_wgsl!("shader.wgsl"));
        let pipeline = |label, polygon_mode, depth_write| {
            create_pipeline(
                &device,
                &pipeline_layout,
                &shader,
                label,
                polygon_mode,
                depth_write,
            )
        };
        let opaque_pipeline = pipeline("Opaque Pipeline", PolygonMode::Fill, true);
        // Transparent surfaces don't hide what's drawn behind them later
        let blend_pipeline = pipeline("Blend Pipeline", PolygonMode::Fill, false);
        let wireframe_pipeline = device
            .features()
            .contains(Features::POLYGON_MODE_LINE)
            .then(|| pipeline("Wireframe Pipeline", PolygonMode::Line, true));

        let gpu_model = GpuModel::new(&device, &queue, model, &material_layout);

        let frame_buffer = device.create_buffer_init(&BufferInitDescriptor {
            label: Some("Frame Uniform Buffer"),
            contents: bytemuck::bytes_of(&FrameUniforms::zeroed()),
            usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
        });
        let frame_bind_group = device.create_bind_group(&BindGroupDescriptor {
            label: Some("Frame Bind Group"),
            layout: &frame_layout,
            entries: &[BindGroupEntry {
                binding: 0,
                resource: frame_buffer.as_entire_binding(),
            }],
        });

        Ok(Self {
            device,
            queue,
            targets,
            opaque_pipeline,
            blend_pipeline,
            wireframe_pipeline,
            wireframe: false,
            model: gpu_model,
            frame_buffer,
            frame_bind_group,
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if (width, height) != (self.targets.width, self.targets.height) {
            self.targets = Targets::new(&self.device, width, height);
        }
    }

    // Switch between filled and wireframe drawing. Stays filled if the GPU
    // can't draw lines.
    pub fn toggle_wireframe(&mut self) {
        self.wireframe = !self.wireframe && self.wireframe_pipeline.is_some();
    }

    // Render a frame and read it back as RGB
    pub fn render(&self, params: &FrameParams) -> Result<Vec<u8>, Box<dyn Error>> {
        let targets = &self.targets;
        let uniforms = FrameUniforms::new(params, !self.model.has_uvs());
        self.queue
            .write_buffer(&self.frame_buffer, 0, bytemuck::bytes_of(&uniforms));

        let opaque_pipeline = match (&self.wireframe_pipeline, self.wireframe) {
            (Some(wireframe), true) => wireframe,
            _ => &self.opaque_pipeline,
        };

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
                        load: LoadOp::Clear(Color::BLACK),
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
            render_pass.set_bind_group(0, &self.frame_bind_group, &[]);
            self.model
                .draw(&mut render_pass, opaque_pipeline, &self.blend_pipeline);
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

fn uniform_entry(binding: u32, visibility: ShaderStages) -> BindGroupLayoutEntry {
    BindGroupLayoutEntry {
        binding,
        visibility,
        ty: BindingType::Buffer {
            ty: BufferBindingType::Uniform,
            has_dynamic_offset: false,
            min_binding_size: None,
        },
        count: None,
    }
}

fn texture_entry(binding: u32) -> BindGroupLayoutEntry {
    BindGroupLayoutEntry {
        binding,
        visibility: ShaderStages::FRAGMENT,
        ty: BindingType::Texture {
            sample_type: TextureSampleType::Float { filterable: true },
            view_dimension: TextureViewDimension::D2,
            multisampled: false,
        },
        count: None,
    }
}

fn create_pipeline(
    device: &Device,
    layout: &PipelineLayout,
    shader: &ShaderModule,
    label: &str,
    polygon_mode: PolygonMode,
    depth_write: bool,
) -> RenderPipeline {
    device.create_render_pipeline(&RenderPipelineDescriptor {
        label: Some(label),
        layout: Some(layout),
        vertex: VertexState {
            module: shader,
            entry_point: Some("vs_main"),
            compilation_options: Default::default(),
            buffers: &[
                Some(VertexBufferLayout {
                    array_stride: mem::size_of::<model::Vertex>() as u64,
                    step_mode: VertexStepMode::Vertex,
                    attributes: &VERTEX_ATTRIBUTES,
                }),
                Some(VertexBufferLayout {
                    array_stride: mem::size_of::<model::Instance>() as u64,
                    step_mode: VertexStepMode::Instance,
                    attributes: &INSTANCE_ATTRIBUTES,
                }),
            ],
        },
        fragment: Some(FragmentState {
            module: shader,
            entry_point: Some("fs_main"),
            compilation_options: Default::default(),
            targets: &[Some(ColorTargetState {
                format: COLOR_FORMAT,
                blend: Some(BlendState {
                    color: BlendComponent {
                        src_factor: BlendFactor::SrcAlpha,
                        dst_factor: BlendFactor::OneMinusSrcAlpha,
                        operation: BlendOperation::Add,
                    },
                    alpha: BlendComponent {
                        src_factor: BlendFactor::One,
                        dst_factor: BlendFactor::OneMinusSrcAlpha,
                        operation: BlendOperation::Add,
                    },
                }),
                write_mask: ColorWrites::ALL,
            })],
        }),
        // Many models aren't closed or have inconsistent winding, so draw both sides
        primitive: PrimitiveState {
            polygon_mode,
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: Some(DepthStencilState {
            format: DEPTH_FORMAT,
            depth_write_enabled: Some(depth_write),
            depth_compare: Some(CompareFunction::Less),
            stencil: StencilState::default(),
            bias: DepthBiasState::default(),
        }),
        multisample: MultisampleState::default(),
        multiview_mask: None,
        cache: None,
    })
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
            view_formats: &[],
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

#[cfg(test)]
mod tests {
    use super::*;

    // WGSL layouts the Rust uniform structs must match
    #[test]
    fn uniform_sizes_match_shader() {
        assert_eq!(mem::size_of::<FrameUniforms>(), 224);
    }

    #[test]
    fn shader_is_valid() {
        let module = naga::front::wgsl::parse_str(include_str!("shader.wgsl")).unwrap();
        naga::valid::Validator::new(
            naga::valid::ValidationFlags::all(),
            naga::valid::Capabilities::empty(),
        )
        .validate(&module)
        .unwrap();
    }
}
