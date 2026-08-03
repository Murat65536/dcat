use std::{error::Error, sync::mpsc::channel};

use wgpu::{
    Buffer, BufferDescriptor, BufferUsages, Color, CommandEncoderDescriptor, Device,
    DeviceDescriptor, Extent3d, FragmentState, Instance, InstanceDescriptor, LoadOp, MapMode,
    MultisampleState, Operations, Origin3d, PollType, PrimitiveState, Queue,
    RenderPassColorAttachment, RenderPassDescriptor, RenderPipeline, RenderPipelineDescriptor,
    RequestAdapterOptions, StoreOp, TexelCopyBufferInfo, TexelCopyBufferLayout,
    TexelCopyTextureInfo, Texture, TextureAspect, TextureDescriptor, TextureDimension,
    TextureFormat, TextureUsages, TextureView, TextureViewDescriptor, VertexState, include_wgsl,
};
pub struct Renderer {
    device: Device,
    queue: Queue,
    render_target: Texture,
    render_target_view: TextureView,
    output_staging_buffer: Buffer,
    pipeline: RenderPipeline,
}

const WIDTH: u64 = 64;
const HEIGHT: u64 = 64;

impl Renderer {
    pub async fn new() -> Result<Self, Box<dyn Error>> {
        let instance = Instance::new(InstanceDescriptor::new_without_display_handle());
        let adapter = instance
            .request_adapter(&RequestAdapterOptions::default())
            .await?;
        let (device, queue) = adapter.request_device(&DeviceDescriptor::default()).await?;

        let shader = device.create_shader_module(include_wgsl!("shader.wgsl"));

        let render_target = device.create_texture(&TextureDescriptor {
            label: None,
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

        let output_staging_buffer = device.create_buffer(&BufferDescriptor {
            label: None,
            size: WIDTH * HEIGHT * 4,
            usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        let pipeline = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: None,
            layout: None,
            vertex: VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            fragment: Some(FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(TextureFormat::Rgba8UnormSrgb.into())],
            }),
            primitive: PrimitiveState::default(),
            depth_stencil: None,
            multisample: MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });

        Ok(Self {
            device,
            queue,
            render_target,
            render_target_view,
            output_staging_buffer,
            pipeline,
        })
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
                depth_stencil_attachment: None,
                occlusion_query_set: None,
                timestamp_writes: None,
                multiview_mask: None,
            });
            render_pass.set_pipeline(&self.pipeline);
            render_pass.draw(0..3, 0..1);
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
