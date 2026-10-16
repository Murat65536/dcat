use std::{
    error::Error,
    f32::consts::PI,
    time::{Duration, Instant},
};

use crossterm::event::{
    self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers, MouseButton, MouseEvent,
    MouseEventKind,
};
use glam::{Mat4, Vec3};
use pollster::block_on;

use crate::{
    camera::{Camera, Framing},
    cli::{Args, OutputMode},
    input::{HeldKey, HeldKeys},
    model::{ModelData, TextureOverrides},
    output::{self, Frame},
    renderer::{FrameParams, Renderer},
    terminal::{self, Terminal},
};

// Orbit step per key press
const ROTATION_AMOUNT: f32 = PI / 8.0;
// Zoom step per key press or scroll notch
const ZOOM_AMOUNT: f32 = 0.05;
// First-person movement in units per second, adjustable with v/b
const INITIAL_MOVE_SPEED: f32 = 0.5;
// First-person look speed in radians per second
const LOOK_SPEED: f32 = 2.0;
// Movement multiplier while ctrl is held
const PRECISE_FACTOR: f32 = 0.25;
// Mouse panning relative to mouse orbit sensitivity
const PAN_FACTOR: f32 = 0.2;

pub struct App {
    args: Args,
    output_mode: OutputMode,
    renderer: Renderer,
    terminal: Terminal,
    width: u32,
    height: u32,
    frame_time: Duration,
    resize_pending: bool,
    should_quit: bool,

    camera: Camera,
    base_model_matrix: Mat4,
    spin_angle: f32,
    move_speed: f32,
    held_keys: HeldKeys,
    last_mouse: (u16, u16),
}

impl App {
    pub fn new(args: Args) -> Result<Self, Box<dyn Error>> {
        let model_path = args
            .model_path
            .as_deref()
            .ok_or("No model file specified")?;
        let output_mode = args
            .output_mode()
            .unwrap_or_else(|| output::detect(|name| std::env::var(name).ok()));
        let (width, height) = terminal::render_size(&args, output_mode)?;

        // Load everything before taking over the terminal so errors and
        // warnings print normally
        let model = ModelData::load(
            model_path,
            TextureOverrides {
                diffuse: args.texture.as_deref(),
                normal: args.normal_map.as_deref(),
            },
        )?;
        let renderer = block_on(Renderer::new(&model, width, height))?;

        let framing = Framing::from_bounds(&model.bounds);
        let base_model_matrix = framing.model_matrix(model.coordinate_system, args.model_scale);
        let camera_position = framing.camera_position(args.model_scale, args.camera_distance);
        let camera = Camera::new(camera_position, Vec3::ZERO, width, height);

        let terminal = Terminal::new(output_mode, args.mouse_orbit, args.keyboard_controls)?;
        let held_keys = HeldKeys::new(terminal.reports_key_releases());

        Ok(Self {
            frame_time: Duration::from_secs_f64(1.0 / f64::from(args.fps)),
            args,
            output_mode,
            renderer,
            terminal,
            width,
            height,
            resize_pending: false,
            should_quit: false,
            camera,
            base_model_matrix,
            spin_angle: 0.0,
            move_speed: INITIAL_MOVE_SPEED,
            held_keys,
            last_mouse: (0, 0),
        })
    }

    pub fn run(&mut self) -> Result<(), Box<dyn Error>> {
        let mut last_frame = Instant::now();

        while !self.should_quit {
            let frame_start = Instant::now();
            let delta_time = (frame_start - last_frame).as_secs_f32();
            last_frame = frame_start;

            if self.resize_pending {
                self.resize()?;
            }

            if self.args.keyboard_controls {
                self.apply_held_keys(delta_time, frame_start);
            } else {
                self.spin_angle += self.args.spin * delta_time;
            }

            let pixels = self.renderer.render(&FrameParams {
                view_projection: self.camera.projection_matrix() * self.camera.view_matrix(),
                model: Mat4::from_rotation_y(self.spin_angle) * self.base_model_matrix,
                camera_position: self.camera.position,
                // Light the model from the camera
                light_direction: -self.camera.forward(),
                lighting: !self.args.no_lighting,
            })?;

            let status = self.args.status_bar.then(|| self.status_line(delta_time));
            let frame = Frame {
                width: self.width,
                height: self.height,
                pixels: &pixels,
            };
            self.terminal.draw(frame, status.as_deref())?;

            self.handle_events_until(frame_start + self.frame_time)?;
        }
        Ok(())
    }

    // Process input until the next frame is due
    fn handle_events_until(&mut self, deadline: Instant) -> Result<(), Box<dyn Error>> {
        while !self.should_quit {
            let timeout = deadline.saturating_duration_since(Instant::now());
            if !event::poll(timeout)? {
                break;
            }
            match event::read()? {
                Event::Key(key) => self.handle_key(key),
                Event::Mouse(mouse) => self.handle_mouse(mouse),
                Event::Resize(..) => self.resize_pending = true,
                _ => {}
            }
        }
        Ok(())
    }

    fn handle_key(&mut self, key: KeyEvent) {
        if self.args.keyboard_controls
            && let Some(held) = HeldKey::from_code(key.code)
        {
            match key.kind {
                KeyEventKind::Press | KeyEventKind::Repeat => {
                    self.held_keys.press(held, Instant::now());
                }
                KeyEventKind::Release => self.held_keys.release(held),
            }
        }

        // Autorepeat counts as more presses for the discrete actions below
        if key.kind == KeyEventKind::Release {
            return;
        }

        let KeyCode::Char(c) = key.code else {
            return;
        };
        match c.to_ascii_lowercase() {
            'q' => self.should_quit = true,
            'c' if key.modifiers.contains(KeyModifiers::CONTROL) => self.should_quit = true,
            'm' => self.renderer.toggle_wireframe(),
            _ if self.args.keyboard_controls => {}
            'a' => self.camera.orbit(ROTATION_AMOUNT, 0.0),
            'd' => self.camera.orbit(-ROTATION_AMOUNT, 0.0),
            'w' => self.camera.orbit(0.0, -ROTATION_AMOUNT),
            's' => self.camera.orbit(0.0, ROTATION_AMOUNT),
            'e' => self.camera.zoom(ZOOM_AMOUNT),
            'r' => self.camera.zoom(-ZOOM_AMOUNT),
            _ => {}
        }
    }

    // Mouse events only arrive when --mouse-orbit enabled capture
    fn handle_mouse(&mut self, mouse: MouseEvent) {
        let position = (mouse.column, mouse.row);
        let delta = (
            f32::from(position.0) - f32::from(self.last_mouse.0),
            f32::from(position.1) - f32::from(self.last_mouse.1),
        );
        let sensitivity = self.args.mouse_sensitivity;

        match mouse.kind {
            MouseEventKind::Down(_) => self.last_mouse = position,
            MouseEventKind::Drag(MouseButton::Left) => {
                self.last_mouse = position;
                self.camera
                    .orbit(delta.0 * sensitivity, -delta.1 * sensitivity);
            }
            MouseEventKind::Drag(MouseButton::Right | MouseButton::Middle) => {
                self.last_mouse = position;
                let pan_speed = sensitivity * PAN_FACTOR;
                self.camera.pan(delta.0 * pan_speed, delta.1 * pan_speed);
            }
            MouseEventKind::ScrollUp => self.camera.zoom(ZOOM_AMOUNT),
            MouseEventKind::ScrollDown => self.camera.zoom(-ZOOM_AMOUNT),
            _ => {}
        }
    }

    // First-person movement from the keys currently held
    fn apply_held_keys(&mut self, delta_time: f32, now: Instant) {
        let held = |key| self.held_keys.is_held(key, now);

        if held(HeldKey::Slower) {
            self.move_speed /= 1.0 + delta_time;
        }
        if held(HeldKey::Faster) {
            self.move_speed *= 1.0 + delta_time;
        }

        let mut step = self.move_speed * delta_time;
        if held(HeldKey::Precise) {
            step *= PRECISE_FACTOR;
        }
        let axis = |positive, negative| match (held(positive), held(negative)) {
            (true, false) => 1.0,
            (false, true) => -1.0,
            _ => 0.0,
        };

        let forward = axis(HeldKey::Forward, HeldKey::Back);
        let right = axis(HeldKey::Right, HeldKey::Left);
        let up = axis(HeldKey::Up, HeldKey::Down);
        let look_yaw = axis(HeldKey::LookRight, HeldKey::LookLeft);
        let look_pitch = axis(HeldKey::LookUp, HeldKey::LookDown);

        self.camera.move_forward(forward * step);
        self.camera.move_right(right * step);
        self.camera.move_up(up * step);

        let look_step = LOOK_SPEED * delta_time;
        self.camera
            .rotate(look_yaw * look_step, look_pitch * look_step);
    }

    fn resize(&mut self) -> Result<(), Box<dyn Error>> {
        self.resize_pending = false;
        let (width, height) = terminal::render_size(&self.args, self.output_mode)?;
        if (width, height) != (self.width, self.height) {
            self.width = width;
            self.height = height;
            self.renderer.resize(width, height);
            self.camera.set_viewport(width, height);
        }
        // The terminal may have shrunk even if the render size is fixed
        self.terminal.clear()?;
        Ok(())
    }

    fn status_line(&self, delta_time: f32) -> String {
        let fps = if delta_time > 0.0 {
            1.0 / delta_time
        } else {
            0.0
        };
        let position = self.camera.position;
        format!(
            " FPS: {fps:.1} | SPEED: {:.2} | POS: {:.2}, {:.2}, {:.2} ",
            self.move_speed, position.x, position.y, position.z
        )
    }
}
