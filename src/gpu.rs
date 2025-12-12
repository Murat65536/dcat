use wgpu::util::DeviceExt;

const VERTEX_DATA_STRIDE: usize = 14;

#[inline]
fn create_gpu_vertex(chunk: &[f32]) -> GpuVertex {
    debug_assert_eq!(
        chunk.len(),
        VERTEX_DATA_STRIDE,
        "chunk must have exactly VERTEX_DATA_STRIDE elements"
    );
    GpuVertex {
        position: [chunk[0], chunk[1], chunk[2]],
        texcoord: [chunk[3], chunk[4]],
        normal: [chunk[5], chunk[6], chunk[7]],
        tangent: [chunk[8], chunk[9], chunk[10]],
        bitangent: [chunk[11], chunk[12], chunk[13]],
    }
}

fn rgb_to_rgba(rgb_data: &[u8], width: u32, height: u32) -> Vec<u8> {
    let pixel_count = (width * height) as usize;
    let expected_size = pixel_count * 3;

    if rgb_data.len() != expected_size {
        panic!(
            "Invalid RGB data size: expected {} bytes ({}x{} pixels * 3), got {} bytes",
            expected_size,
            width,
            height,
            rgb_data.len()
        );
    }

    let mut rgba_data = Vec::with_capacity(pixel_count * 4);

    for chunk in rgb_data.chunks_exact(3) {
        rgba_data.push(chunk[0]); // R
        rgba_data.push(chunk[1]); // G
        rgba_data.push(chunk[2]); // B
        rgba_data.push(255); // A
    }

    rgba_data
}

fn align_bytes_per_row(width: u32, bytes_per_pixel: u32) -> u32 {
    let unaligned = width * bytes_per_pixel;
    let align = wgpu::COPY_BYTES_PER_ROW_ALIGNMENT;
    // Round up to next multiple of alignment
    unaligned.div_ceil(align) * align
}

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct Uniforms {
    mvp: [[f32; 4]; 4],
    model: [[f32; 4]; 4],
}

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct FragmentUniforms {
    light_dir: [f32; 3],
    enable_lighting: u32,
}

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct GpuVertex {
    position: [f32; 3],
    texcoord: [f32; 2],
    normal: [f32; 3],
    tangent: [f32; 3],
    bitangent: [f32; 3],
}

impl GpuVertex {
    fn desc<'a>() -> wgpu::VertexBufferLayout<'a> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<GpuVertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                // position
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                // texcoord
                wgpu::VertexAttribute {
                    offset: std::mem::size_of::<[f32; 3]>() as wgpu::BufferAddress,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x2,
                },
                // normal
                wgpu::VertexAttribute {
                    offset: std::mem::size_of::<[f32; 5]>() as wgpu::BufferAddress,
                    shader_location: 2,
                    format: wgpu::VertexFormat::Float32x3,
                },
                // tangent
                wgpu::VertexAttribute {
                    offset: std::mem::size_of::<[f32; 8]>() as wgpu::BufferAddress,
                    shader_location: 3,
                    format: wgpu::VertexFormat::Float32x3,
                },
                // bitangent
                wgpu::VertexAttribute {
                    offset: std::mem::size_of::<[f32; 11]>() as wgpu::BufferAddress,
                    shader_location: 4,
                    format: wgpu::VertexFormat::Float32x3,
                },
            ],
        }
    }
}

pub struct TextureParams<'a> {
    pub data: &'a [u8],
    pub width: u32,
    pub height: u32,
}

pub struct RenderParams<'a> {
    pub mvp: &'a [[f32; 4]; 4],
    pub model: &'a [[f32; 4]; 4],
    pub texture: &'a TextureParams<'a>,
    pub normal_map: &'a TextureParams<'a>,
    pub enable_lighting: bool,
}

pub struct GpuRenderer {
    device: wgpu::Device,
    queue: wgpu::Queue,
    render_pipeline: wgpu::RenderPipeline,
    uniform_buffer: wgpu::Buffer,
    fragment_uniform_buffer: wgpu::Buffer,
    bind_group_layout: wgpu::BindGroupLayout,
    width: u32,
    height: u32,
    padded_bytes_per_row: u32,
    output_buffer: wgpu::Buffer,
    output_texture: wgpu::Texture,
    depth_texture: wgpu::Texture,
    cached_sampler: Option<wgpu::Sampler>,
    cached_diffuse_texture: Option<(wgpu::Texture, u32, u32)>,
    cached_normal_texture: Option<(wgpu::Texture, u32, u32)>,
    cached_bind_group: Option<wgpu::BindGroup>,
    cached_vertex_buffer: Option<(wgpu::Buffer, usize)>,
    cached_index_buffer: Option<(wgpu::Buffer, usize)>,
    diffuse_texture_uploaded: bool,
    normal_texture_uploaded: bool,
    cached_rgb_buffer: Vec<u8>,
    cached_rgba_diffuse: Option<Vec<u8>>,
    cached_rgba_normal: Option<Vec<u8>>,
    cached_diffuse_view: Option<wgpu::TextureView>,
    cached_normal_view: Option<wgpu::TextureView>,
    cached_output_view: Option<wgpu::TextureView>,
    cached_depth_view: Option<wgpu::TextureView>,
    cached_gpu_vertices: Vec<GpuVertex>,
    normalized_light_dir: [f32; 3],
}

impl GpuRenderer {
    #[inline]
    fn calculate_rgb_buffer_size(&self) -> usize {
        (self.width * self.height * 3) as usize
    }

    fn create_size_dependent_resources(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> (wgpu::Texture, wgpu::Texture, wgpu::Buffer, u32) {
        let texture_format = wgpu::TextureFormat::Rgba8UnormSrgb;

        let output_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Output Texture"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: texture_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });

        let depth_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Depth Texture"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });

        let padded_bytes_per_row = align_bytes_per_row(width, 4);
        let output_buffer_size = (padded_bytes_per_row * height) as u64;
        let output_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Output Buffer"),
            size: output_buffer_size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        (
            output_texture,
            depth_texture,
            output_buffer,
            padded_bytes_per_row,
        )
    }

    pub async fn new(width: u32, height: u32) -> Result<Self, Box<dyn std::error::Error>> {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });

        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: None,
                force_fallback_adapter: false,
            })
            .await?;

        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("Render Device"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::default(),
                memory_hints: wgpu::MemoryHints::Performance,
                experimental_features: Default::default(),
                trace: Default::default(),
            })
            .await?;

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("../shader.wgsl").into()),
        });

        let uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Uniform Buffer"),
            size: std::mem::size_of::<Uniforms>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let fragment_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Fragment Uniform Buffer"),
            size: std::mem::size_of::<FragmentUniforms>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Bind Group Layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 3,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 4,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Render Pipeline Layout"),
            bind_group_layouts: &[&bind_group_layout],
            push_constant_ranges: &[],
        });

        let texture_format = wgpu::TextureFormat::Rgba8UnormSrgb;

        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Render Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[GpuVertex::desc()],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: texture_format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                strip_index_format: None,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: Some(wgpu::Face::Back),
                polygon_mode: wgpu::PolygonMode::Fill,
                unclipped_depth: false,
                conservative: false,
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: wgpu::TextureFormat::Depth32Float,
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Less,
                stencil: wgpu::StencilState::default(),
                bias: wgpu::DepthBiasState::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: 1,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            multiview: None,
            cache: None,
        });

        let (output_texture, depth_texture, output_buffer, padded_bytes_per_row) =
            Self::create_size_dependent_resources(&device, width, height);

        let rgb_buffer_size = (width * height * 3) as usize;
        let cached_rgb_buffer = Vec::with_capacity(rgb_buffer_size);

        let cached_output_view =
            Some(output_texture.create_view(&wgpu::TextureViewDescriptor::default()));
        let cached_depth_view =
            Some(depth_texture.create_view(&wgpu::TextureViewDescriptor::default()));

        let light_dir = [0.0_f32, 0.5_f32, 0.3_f32];
        let length = (light_dir[0] * light_dir[0]
            + light_dir[1] * light_dir[1]
            + light_dir[2] * light_dir[2])
            .sqrt();
        let normalized_light_dir = [
            light_dir[0] / length,
            light_dir[1] / length,
            light_dir[2] / length,
        ];

        Ok(Self {
            device,
            queue,
            render_pipeline,
            uniform_buffer,
            fragment_uniform_buffer,
            bind_group_layout,
            width,
            height,
            padded_bytes_per_row,
            output_buffer,
            output_texture,
            depth_texture,
            cached_sampler: None,
            cached_diffuse_texture: None,
            cached_normal_texture: None,
            cached_bind_group: None,
            cached_vertex_buffer: None,
            cached_index_buffer: None,
            diffuse_texture_uploaded: false,
            normal_texture_uploaded: false,
            cached_rgb_buffer,
            cached_rgba_diffuse: None,
            cached_rgba_normal: None,
            cached_diffuse_view: None,
            cached_normal_view: None,
            cached_output_view,
            cached_depth_view,
            cached_gpu_vertices: Vec::new(),
            normalized_light_dir,
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        self.width = width;
        self.height = height;

        let (output_texture, depth_texture, output_buffer, padded_bytes_per_row) =
            Self::create_size_dependent_resources(&self.device, width, height);

        self.output_texture = output_texture;
        self.depth_texture = depth_texture;
        self.output_buffer = output_buffer;
        self.padded_bytes_per_row = padded_bytes_per_row;

        self.cached_output_view = Some(
            self.output_texture
                .create_view(&wgpu::TextureViewDescriptor::default()),
        );
        self.cached_depth_view = Some(
            self.depth_texture
                .create_view(&wgpu::TextureViewDescriptor::default()),
        );

        // Resize the cached RGB buffer capacity
        self.cached_rgb_buffer.clear();
        let rgb_buffer_size = self.calculate_rgb_buffer_size();
        if self.cached_rgb_buffer.capacity() < rgb_buffer_size {
            self.cached_rgb_buffer = Vec::with_capacity(rgb_buffer_size);
        }
    }

    pub fn set_light_direction(&mut self, direction: [f32; 3]) {
        let length = (direction[0] * direction[0]
            + direction[1] * direction[1]
            + direction[2] * direction[2])
            .sqrt();
        self.normalized_light_dir = [
            direction[0] / length,
            direction[1] / length,
            direction[2] / length,
        ];
    }

    pub fn render(
        &mut self,
        vertices: &[f32],
        indices: &[u32],
        params: &RenderParams,
    ) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
        let vertex_count = vertices.len() / VERTEX_DATA_STRIDE;
        if self.cached_gpu_vertices.len() != vertex_count {
            self.cached_gpu_vertices = vertices
                .chunks(VERTEX_DATA_STRIDE)
                .map(create_gpu_vertex)
                .collect();
        } else {
            self.cached_gpu_vertices
                .iter_mut()
                .zip(vertices.chunks(VERTEX_DATA_STRIDE))
                .for_each(|(vertex, chunk)| {
                    *vertex = create_gpu_vertex(chunk);
                });
        }

        let vertex_buffer = if let Some((ref buffer, cached_count)) = self.cached_vertex_buffer {
            if cached_count == self.cached_gpu_vertices.len() {
                self.queue
                    .write_buffer(buffer, 0, bytemuck::cast_slice(&self.cached_gpu_vertices));
                buffer
            } else {
                let new_buffer =
                    self.device
                        .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                            label: Some("Vertex Buffer"),
                            contents: bytemuck::cast_slice(&self.cached_gpu_vertices),
                            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                        });
                self.cached_vertex_buffer = Some((new_buffer, self.cached_gpu_vertices.len()));
                &self.cached_vertex_buffer.as_ref().unwrap().0
            }
        } else {
            let new_buffer = self
                .device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Vertex Buffer"),
                    contents: bytemuck::cast_slice(&self.cached_gpu_vertices),
                    usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                });
            self.cached_vertex_buffer = Some((new_buffer, self.cached_gpu_vertices.len()));
            &self.cached_vertex_buffer.as_ref().unwrap().0
        };

        let index_buffer = if let Some((ref buffer, cached_count)) = self.cached_index_buffer {
            if cached_count == indices.len() {
                self.queue
                    .write_buffer(buffer, 0, bytemuck::cast_slice(indices));
                buffer
            } else {
                let new_buffer =
                    self.device
                        .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                            label: Some("Index Buffer"),
                            contents: bytemuck::cast_slice(indices),
                            usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
                        });
                self.cached_index_buffer = Some((new_buffer, indices.len()));
                &self.cached_index_buffer.as_ref().unwrap().0
            }
        } else {
            let new_buffer = self
                .device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: Some("Index Buffer"),
                    contents: bytemuck::cast_slice(indices),
                    usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
                });
            self.cached_index_buffer = Some((new_buffer, indices.len()));
            &self.cached_index_buffer.as_ref().unwrap().0
        };

        let uniforms = Uniforms {
            mvp: *params.mvp,
            model: *params.model,
        };
        self.queue
            .write_buffer(&self.uniform_buffer, 0, bytemuck::bytes_of(&uniforms));

        let fragment_uniforms = FragmentUniforms {
            light_dir: self.normalized_light_dir,
            enable_lighting: if params.enable_lighting { 1 } else { 0 },
        };
        self.queue.write_buffer(
            &self.fragment_uniform_buffer,
            0,
            bytemuck::bytes_of(&fragment_uniforms),
        );

        let _diffuse_texture =
            if let Some((ref tex, cached_width, cached_height)) = self.cached_diffuse_texture {
                if cached_width == params.texture.width && cached_height == params.texture.height {
                    if !self.diffuse_texture_uploaded {
                        let rgba_texture_data = if let Some(ref cached) = self.cached_rgba_diffuse {
                            cached
                        } else {
                            let rgba = rgb_to_rgba(
                                params.texture.data,
                                params.texture.width,
                                params.texture.height,
                            );
                            self.cached_rgba_diffuse = Some(rgba);
                            self.cached_rgba_diffuse.as_ref().unwrap()
                        };

                        self.queue.write_texture(
                            wgpu::TexelCopyTextureInfo {
                                texture: tex,
                                mip_level: 0,
                                origin: wgpu::Origin3d::ZERO,
                                aspect: wgpu::TextureAspect::All,
                            },
                            rgba_texture_data,
                            wgpu::TexelCopyBufferLayout {
                                offset: 0,
                                bytes_per_row: Some(4 * params.texture.width),
                                rows_per_image: Some(params.texture.height),
                            },
                            wgpu::Extent3d {
                                width: params.texture.width,
                                height: params.texture.height,
                                depth_or_array_layers: 1,
                            },
                        );
                        self.diffuse_texture_uploaded = true;
                    }
                    tex
                } else {
                    self.diffuse_texture_uploaded = false;
                    self.cached_rgba_diffuse = None;

                    let rgba_texture_data = rgb_to_rgba(
                        params.texture.data,
                        params.texture.width,
                        params.texture.height,
                    );
                    self.cached_rgba_diffuse = Some(rgba_texture_data);
                    let rgba_ref = self.cached_rgba_diffuse.as_ref().unwrap();

                    let new_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                        label: Some("Diffuse Texture"),
                        size: wgpu::Extent3d {
                            width: params.texture.width,
                            height: params.texture.height,
                            depth_or_array_layers: 1,
                        },
                        mip_level_count: 1,
                        sample_count: 1,
                        dimension: wgpu::TextureDimension::D2,
                        format: wgpu::TextureFormat::Rgba8UnormSrgb,
                        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                        view_formats: &[],
                    });

                    self.queue.write_texture(
                        wgpu::TexelCopyTextureInfo {
                            texture: &new_texture,
                            mip_level: 0,
                            origin: wgpu::Origin3d::ZERO,
                            aspect: wgpu::TextureAspect::All,
                        },
                        rgba_ref,
                        wgpu::TexelCopyBufferLayout {
                            offset: 0,
                            bytes_per_row: Some(4 * params.texture.width),
                            rows_per_image: Some(params.texture.height),
                        },
                        wgpu::Extent3d {
                            width: params.texture.width,
                            height: params.texture.height,
                            depth_or_array_layers: 1,
                        },
                    );

                    self.cached_diffuse_texture =
                        Some((new_texture, params.texture.width, params.texture.height));
                    self.diffuse_texture_uploaded = true;
                    let new_view = self
                        .cached_diffuse_texture
                        .as_ref()
                        .unwrap()
                        .0
                        .create_view(&wgpu::TextureViewDescriptor::default());
                    self.cached_diffuse_view = Some(new_view);
                    self.cached_bind_group = None;
                    &self.cached_diffuse_texture.as_ref().unwrap().0
                }
            } else {
                let rgba_texture_data = rgb_to_rgba(
                    params.texture.data,
                    params.texture.width,
                    params.texture.height,
                );
                self.cached_rgba_diffuse = Some(rgba_texture_data);
                let rgba_ref = self.cached_rgba_diffuse.as_ref().unwrap();

                let new_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                    label: Some("Diffuse Texture"),
                    size: wgpu::Extent3d {
                        width: params.texture.width,
                        height: params.texture.height,
                        depth_or_array_layers: 1,
                    },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Rgba8UnormSrgb,
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                    view_formats: &[],
                });

                self.queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &new_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    rgba_ref,
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(4 * params.texture.width),
                        rows_per_image: Some(params.texture.height),
                    },
                    wgpu::Extent3d {
                        width: params.texture.width,
                        height: params.texture.height,
                        depth_or_array_layers: 1,
                    },
                );

                self.cached_diffuse_texture =
                    Some((new_texture, params.texture.width, params.texture.height));
                self.diffuse_texture_uploaded = true;
                let new_view = self
                    .cached_diffuse_texture
                    .as_ref()
                    .unwrap()
                    .0
                    .create_view(&wgpu::TextureViewDescriptor::default());
                self.cached_diffuse_view = Some(new_view);
                &self.cached_diffuse_texture.as_ref().unwrap().0
            };

        let _normal_texture = if let Some((ref tex, cached_width, cached_height)) =
            self.cached_normal_texture
        {
            if cached_width == params.normal_map.width && cached_height == params.normal_map.height
            {
                if !self.normal_texture_uploaded {
                    let rgba_normal_data = if let Some(ref cached) = self.cached_rgba_normal {
                        cached
                    } else {
                        let rgba = rgb_to_rgba(
                            params.normal_map.data,
                            params.normal_map.width,
                            params.normal_map.height,
                        );
                        self.cached_rgba_normal = Some(rgba);
                        self.cached_rgba_normal.as_ref().unwrap()
                    };

                    self.queue.write_texture(
                        wgpu::TexelCopyTextureInfo {
                            texture: tex,
                            mip_level: 0,
                            origin: wgpu::Origin3d::ZERO,
                            aspect: wgpu::TextureAspect::All,
                        },
                        rgba_normal_data,
                        wgpu::TexelCopyBufferLayout {
                            offset: 0,
                            bytes_per_row: Some(4 * params.normal_map.width),
                            rows_per_image: Some(params.normal_map.height),
                        },
                        wgpu::Extent3d {
                            width: params.normal_map.width,
                            height: params.normal_map.height,
                            depth_or_array_layers: 1,
                        },
                    );
                    self.normal_texture_uploaded = true;
                }
                tex
            } else {
                self.normal_texture_uploaded = false;
                self.cached_rgba_normal = None;

                let rgba_normal_data = rgb_to_rgba(
                    params.normal_map.data,
                    params.normal_map.width,
                    params.normal_map.height,
                );
                self.cached_rgba_normal = Some(rgba_normal_data);
                let rgba_ref = self.cached_rgba_normal.as_ref().unwrap();

                let new_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                    label: Some("Normal Texture"),
                    size: wgpu::Extent3d {
                        width: params.normal_map.width,
                        height: params.normal_map.height,
                        depth_or_array_layers: 1,
                    },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Rgba8UnormSrgb,
                    usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                    view_formats: &[],
                });

                self.queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &new_texture,
                        mip_level: 0,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    rgba_ref,
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(4 * params.normal_map.width),
                        rows_per_image: Some(params.normal_map.height),
                    },
                    wgpu::Extent3d {
                        width: params.normal_map.width,
                        height: params.normal_map.height,
                        depth_or_array_layers: 1,
                    },
                );

                self.cached_normal_texture = Some((
                    new_texture,
                    params.normal_map.width,
                    params.normal_map.height,
                ));
                self.normal_texture_uploaded = true;
                let new_view = self
                    .cached_normal_texture
                    .as_ref()
                    .unwrap()
                    .0
                    .create_view(&wgpu::TextureViewDescriptor::default());
                self.cached_normal_view = Some(new_view);
                self.cached_bind_group = None;
                &self.cached_normal_texture.as_ref().unwrap().0
            }
        } else {
            let rgba_normal_data = rgb_to_rgba(
                params.normal_map.data,
                params.normal_map.width,
                params.normal_map.height,
            );
            self.cached_rgba_normal = Some(rgba_normal_data);
            let rgba_ref = self.cached_rgba_normal.as_ref().unwrap();

            let new_texture = self.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("Normal Texture"),
                size: wgpu::Extent3d {
                    width: params.normal_map.width,
                    height: params.normal_map.height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8UnormSrgb,
                usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                view_formats: &[],
            });

            self.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &new_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                rgba_ref,
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(4 * params.normal_map.width),
                    rows_per_image: Some(params.normal_map.height),
                },
                wgpu::Extent3d {
                    width: params.normal_map.width,
                    height: params.normal_map.height,
                    depth_or_array_layers: 1,
                },
            );

            self.cached_normal_texture = Some((
                new_texture,
                params.normal_map.width,
                params.normal_map.height,
            ));
            self.normal_texture_uploaded = true;
            let new_view = self
                .cached_normal_texture
                .as_ref()
                .unwrap()
                .0
                .create_view(&wgpu::TextureViewDescriptor::default());
            self.cached_normal_view = Some(new_view);
            &self.cached_normal_texture.as_ref().unwrap().0
        };

        let diffuse_view = self
            .cached_diffuse_view
            .as_ref()
            .expect("Diffuse texture view should be cached");
        let normal_view = self
            .cached_normal_view
            .as_ref()
            .expect("Normal texture view should be cached");

        let sampler = if let Some(ref cached_sampler) = self.cached_sampler {
            cached_sampler
        } else {
            let new_sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
                label: Some("Texture Sampler"),
                address_mode_u: wgpu::AddressMode::Repeat,
                address_mode_v: wgpu::AddressMode::Repeat,
                address_mode_w: wgpu::AddressMode::Repeat,
                mag_filter: wgpu::FilterMode::Linear,
                min_filter: wgpu::FilterMode::Linear,
                mipmap_filter: wgpu::FilterMode::Nearest,
                ..Default::default()
            });
            self.cached_sampler = Some(new_sampler);
            self.cached_sampler.as_ref().unwrap()
        };

        let bind_group = if let Some(ref cached_bind_group) = self.cached_bind_group {
            cached_bind_group
        } else {
            let new_bind_group = self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Bind Group"),
                layout: &self.bind_group_layout,
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: self.uniform_buffer.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: wgpu::BindingResource::Sampler(sampler),
                    },
                    wgpu::BindGroupEntry {
                        binding: 2,
                        resource: wgpu::BindingResource::TextureView(diffuse_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 3,
                        resource: wgpu::BindingResource::TextureView(normal_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 4,
                        resource: self.fragment_uniform_buffer.as_entire_binding(),
                    },
                ],
            });
            self.cached_bind_group = Some(new_bind_group);
            self.cached_bind_group.as_ref().unwrap()
        };

        let output_view = self
            .cached_output_view
            .as_ref()
            .expect("Output texture view should be cached");

        let depth_view = self
            .cached_depth_view
            .as_ref()
            .expect("Depth texture view should be cached");

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Render Encoder"),
            });

        {
            let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Render Pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: output_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                    depth_slice: None,
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(1.0),
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
            });

            render_pass.set_pipeline(&self.render_pipeline);
            render_pass.set_bind_group(0, bind_group, &[]);
            render_pass.set_vertex_buffer(0, vertex_buffer.slice(..));
            render_pass.set_index_buffer(index_buffer.slice(..), wgpu::IndexFormat::Uint32);
            render_pass.draw_indexed(0..indices.len() as u32, 0, 0..1);
        }

        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &self.output_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &self.output_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.padded_bytes_per_row),
                    rows_per_image: Some(self.height),
                },
            },
            wgpu::Extent3d {
                width: self.width,
                height: self.height,
                depth_or_array_layers: 1,
            },
        );

        self.queue.submit(std::iter::once(encoder.finish()));

        let buffer_slice = self.output_buffer.slice(..);
        let (sender, receiver) = std::sync::mpsc::channel();
        buffer_slice.map_async(wgpu::MapMode::Read, move |result| {
            sender.send(result).ok();
        });

        self.device.poll(wgpu::PollType::Wait {
            submission_index: None,
            timeout: None,
        })?;
        receiver
            .recv()
            .map_err(|e| Box::new(e) as Box<dyn std::error::Error>)??;

        let data = buffer_slice.get_mapped_range();

        self.cached_rgb_buffer.clear();
        let bytes_per_pixel = 4; // RGBA
        let unpadded_bytes_per_row = self.width * bytes_per_pixel;

        let total_rgb_bytes = (self.width * self.height * 3) as usize;
        self.cached_rgb_buffer.reserve(total_rgb_bytes);

        for row in 0..self.height {
            let row_start = (row * self.padded_bytes_per_row) as usize;
            let row_end = row_start + unpadded_bytes_per_row as usize;
            let row_data = &data[row_start..row_end];

            for chunk in row_data.chunks_exact(4) {
                self.cached_rgb_buffer.extend_from_slice(&chunk[0..3]);
            }
        }

        drop(data);
        self.output_buffer.unmap();

        let replacement_buffer = Vec::with_capacity(self.calculate_rgb_buffer_size());
        let result = std::mem::replace(&mut self.cached_rgb_buffer, replacement_buffer);

        Ok(result)
    }
}
