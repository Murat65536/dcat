use cgmath::{Deg, Matrix4, Rad, InnerSpace};
use clap::Parser;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::io::{self, Write};
use std::os::fd::AsRawFd;
use termion::cursor::HideCursor;
use termion::screen::IntoAlternateScreen;
use termion::raw::IntoRawMode;
use input_query::{InputHandler, KeyCode};

use crate::camera::Camera;
use crate::cli::Args;
use crate::gpu::{RenderParams, TextureParams};
use crate::model::{load_obj, calculate_camera_setup};
use crate::terminal::{
    calculate_render_dimensions, render_kitty,
    render_sixel, render_terminal,
};
use crate::texture::Texture;

mod camera;
mod cli;
mod gpu;
mod model;
mod terminal;
mod texture;
mod vertex;

fn main() {
    let args = Args::parse();

    let terminal_guard = HideCursor::from(std::io::stdout());
    
    // Enable terminal focus tracking
    print!("\x1b[?1004h");
    io::stdout().flush().ok();

    let result = run(terminal_guard, args);
    
    // Disable terminal focus tracking
    print!("\x1b[?1004l");
    io::stdout().flush().ok();

    if let Err(e) = result {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}

fn run(terminal_guard: HideCursor<std::io::Stdout>, args: Args) -> Result<(), Box<dyn std::error::Error>> {
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();

    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })
    .expect("Error setting Ctrl+C handler");

    let use_kitty = args.kitty;

    let use_sixel = args.sixel;

    let (width, height) =
        calculate_render_dimensions(args.width, args.height, use_sixel, use_kitty);

    let mut gpu_renderer = match pollster::block_on(gpu::GpuRenderer::new(width, height)) {
        Ok(renderer) => renderer,
        Err(e) => {
            return Err(format!("Failed to initialize GPU renderer: {}. Please ensure your system has a compatible GPU with Vulkan, Metal, or DirectX 12 support.", e).into());
        }
    };

    let texture_handle = args.texture.clone().map(|texture_path| {
        std::thread::spawn(move || {
            Texture::from_image(&texture_path).unwrap_or_else(|e| {
                eprintln!("Warning: Failed to load texture ({}), using gray", e);
                Texture::new(1, 1)
            })
        })
    });

    let normal_map_handle = args.normal_map.clone().map(|normal_map_path| {
        std::thread::spawn(move || {
            Texture::from_image(&normal_map_path).unwrap_or_else(|e| {
                eprintln!(
                    "Warning: Failed to load normal map ({}), using flat normals",
                    e
                );
                Texture::create_flat_normal_map()
            })
        })
    });

    let model_handle = args.model;

    let model_thread = std::thread::spawn(move || {
        load_obj(&model_handle)
    });

    let texture = if let Some(handle) = texture_handle {
        handle.join().unwrap_or_else(|_| {
            eprintln!("Error: Texture loading failed. Check if the texture file exists and is in a supported format. Using fallback gray texture.");
            Texture::new(1, 1)
        })
    } else {
        Texture::new(1, 1)
    };

    let normal_map = if let Some(handle) = normal_map_handle {
        handle.join().unwrap_or_else(|_| {
            eprintln!("Error: Normal map loading failed. Using fallback flat normal map.");
            Texture::create_flat_normal_map()
        })
    } else {
        Texture::create_flat_normal_map()
    };

    let (vertices, indices) = model_thread
        .join()
        .map_err(|_| "Model loading thread panicked")?
        .map_err(|e| format!("Failed to load model: {}", e))?;

    let camera_setup = calculate_camera_setup(&vertices);
    
    let (camera_position, camera_target) = if let Some(distance) = args.camera_distance {
        let direction = (camera_setup.position - camera_setup.target).normalize();
        let position = camera_setup.target + direction * distance;
        (position, camera_setup.target)
    } else {
        (camera_setup.position, camera_setup.target)
    };

    let mut cpu_vertex_data = Vec::with_capacity(vertices.len() * 14);

    for vertex in &vertices {
        cpu_vertex_data.extend_from_slice(&[
            vertex.position.x,
            vertex.position.y,
            vertex.position.z,
            vertex.texcoord.x,
            vertex.texcoord.y,
            vertex.normal.x,
            vertex.normal.y,
            vertex.normal.z,
            vertex.tangent.x,
            vertex.tangent.y,
            vertex.tangent.z,
            vertex.bitangent.x,
            vertex.bitangent.y,
            vertex.bitangent.z,
        ]);
    }

    const MODEL_ROTATION_SPEED: f32 = 0.6;
    const MOVE_SPEED_BASE: f32 = 0.5;
    const ROTATION_SENSITIVITY: f32 = 2.0;
    const TARGET_FPS: u64 = 60;
    const FRAME_TIME: std::time::Duration = std::time::Duration::from_micros(1_000_000 / TARGET_FPS);
    
    // Scale movement speed with model size
    let move_speed = MOVE_SPEED_BASE * camera_setup.model_scale;

    // Enter raw mode and alternate screen
    let mut _raw_mode = std::io::stdout().into_raw_mode()?;
    let mut _alternate_screen = terminal_guard.into_alternate_screen()?;

    let mut accumulated_time = 0.0_f32;
    let mut last_frame_time: Option<std::time::Instant> = None;

    let mut current_width = width;
    let mut current_height = height;
    let mut camera = Camera::new(width, height, camera_position, camera_target, Deg(60.0));
    let mut view = camera.view_matrix();
    let mut projection = camera.projection_matrix();

    let query = if args.fps_controls {
        Some(InputHandler::new())
    } else {
        None
    };
    
    let is_focused = Arc::new(AtomicBool::new(true));
    let focus_clone = is_focused.clone();
    let running_clone = running.clone();
    
    // Spawn thread to read focus events and key presses from stdin
    std::thread::spawn(move || {
        use std::io::stdin;
        
        let stdin_fd = stdin().as_raw_fd();
        
        let mut buffer = [0u8; 64];
        loop {
            // Use poll to check if data is available
            let mut pollfd = libc::pollfd {
                fd: stdin_fd,
                events: libc::POLLIN,
                revents: 0,
            };
            
            unsafe {
                // Poll with 16ms timeout
                let ret = libc::poll(&mut pollfd as *mut libc::pollfd, 1, 16);
                
                if ret > 0 && (pollfd.revents & libc::POLLIN) != 0 {
                    // Data available, try to read
                    let n = libc::read(stdin_fd, buffer.as_mut_ptr() as *mut libc::c_void, buffer.len());
                    
                    if n > 0 {
                        let n = n as usize;
                        // Check for 'q' key press
                        for i in 0..n {
                            if buffer[i] == b'q' || buffer[i] == b'Q' {
                                running_clone.store(false, Ordering::SeqCst);
                                return;
                            }
                        }
                        // Scan for focus sequences
                        for i in 0..n.saturating_sub(2) {
                            if buffer[i] == 0x1b && buffer[i + 1] == b'[' {
                                if buffer[i + 2] == b'I' {
                                    focus_clone.store(true, Ordering::SeqCst);
                                } else if buffer[i + 2] == b'O' {
                                    focus_clone.store(false, Ordering::SeqCst);
                                }
                            }
                        }
                    }
                }
            }
        }
    });

    loop {
        if !running.load(Ordering::SeqCst) {
            break;
        }

        let (new_width, new_height) =
            calculate_render_dimensions(args.width, args.height, use_sixel, use_kitty);
        if new_width != current_width || new_height != current_height {
            current_width = new_width;
            current_height = new_height;

            gpu_renderer.resize(current_width, current_height);

            camera = Camera::new(
                current_width,
                current_height,
                camera.position,
                camera.target,
                Deg(60.0),
            );
            view = camera.view_matrix();
            projection = camera.projection_matrix();
        }

        let frame_start = std::time::Instant::now();
        let delta_time = if let Some(last) = last_frame_time {
            frame_start.duration_since(last).as_secs_f32()
        } else {
            0.0
        };
        last_frame_time = Some(frame_start);

        accumulated_time += delta_time;

        // Handle FPS camera controls (only when focused)
        if let Some(ref query) = query {
            if is_focused.load(Ordering::SeqCst) {
                // Check for quit key
                if query.is_pressed(KeyCode::KeyQ) {
                    break;
                }
                
                let movement_amount = move_speed * delta_time;
                let rotation_amount = ROTATION_SENSITIVITY * delta_time;

                if query.is_pressed(KeyCode::KeyW) {
                    camera.move_forward(movement_amount);
                }
                if query.is_pressed(KeyCode::KeyS) {
                    camera.move_backward(movement_amount);
                }
                if query.is_pressed(KeyCode::KeyA) {
                    camera.move_left(movement_amount);
                }
                if query.is_pressed(KeyCode::KeyD) {
                    camera.move_right(movement_amount);
                }
                if query.is_pressed(KeyCode::KeySpace) {
                    camera.move_up(movement_amount);
                }
                if query.is_pressed(KeyCode::KeyLeftShift) {
                    camera.move_down(movement_amount);
                }
                
                if query.is_pressed(KeyCode::KeyLeft) {
                    camera.rotate(-rotation_amount, 0.0);
                }
                if query.is_pressed(KeyCode::KeyRight) {
                    camera.rotate(rotation_amount, 0.0);
                }
                if query.is_pressed(KeyCode::KeyUp) {
                    camera.rotate(0.0, rotation_amount);
                }
                if query.is_pressed(KeyCode::KeyDown) {
                    camera.rotate(0.0, -rotation_amount);
                }

                view = camera.view_matrix();
            }
        }

        let angle = accumulated_time * MODEL_ROTATION_SPEED;

        let model = if args.fps_controls {
            Matrix4::from_angle_y(Rad(0.0))
        } else {
            Matrix4::from_angle_y(Rad(angle * 0.7))
        };

        let mvp = projection * view * model;

        let mvp_array: [[f32; 4]; 4] = mvp.into();
        let model_array: [[f32; 4]; 4] = model.into();

        let forward = camera.forward_direction();
        gpu_renderer.set_light_direction([-forward.x, -forward.y, -forward.z]);

        let texture_params = TextureParams {
            data: &texture.data,
            width: texture.width,
            height: texture.height,
        };
        let normal_map_params = TextureParams {
            data: &normal_map.data,
            width: normal_map.width,
            height: normal_map.height,
        };
        let render_params = RenderParams {
            mvp: &mvp_array,
            model: &model_array,
            texture: &texture_params,
            normal_map: &normal_map_params,
            enable_lighting: !args.no_lighting,
        };
        let render_result = gpu_renderer.render(&cpu_vertex_data, &indices, &render_params);

        match render_result {
            Ok(fb) => {
                if use_kitty {
                    render_kitty(&fb, current_width, current_height);
                } else if use_sixel {
                    render_sixel(&fb, current_width, current_height);
                } else {
                    render_terminal(&fb, current_width, current_height);
                }
            }
            Err(e) => {
                return Err(format!("Rendering failure: {}", e).into());
            }
        }
        
        // Limit frame rate to reduce CPU usage
        let frame_duration = frame_start.elapsed();
        if frame_duration < FRAME_TIME {
            std::thread::sleep(FRAME_TIME - frame_duration);
        }
    }

    Ok(())
}
