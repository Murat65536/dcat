use std::{
    error::Error,
    time::{Duration, Instant},
};

use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use pollster::block_on;

use crate::{
    cli::Args,
    renderer::Renderer,
    terminal::{self, Terminal},
};

pub struct App {
    args: Args,
    renderer: Renderer,
    terminal: Terminal,
    width: u32,
    height: u32,
    frame_time: Duration,
    resize_pending: bool,
    should_quit: bool,
}

impl App {
    pub fn new(args: Args) -> Result<Self, Box<dyn Error>> {
        let model_path = args
            .model_path
            .as_deref()
            .ok_or("No model file specified")?;
        let (width, height) = terminal::render_size(&args)?;

        // Set up the GPU before taking over the terminal so errors print normally
        let renderer = block_on(Renderer::new(model_path, width, height))?;
        let terminal = Terminal::new()?;

        Ok(Self {
            frame_time: Duration::from_secs_f64(1.0 / f64::from(args.fps)),
            args,
            renderer,
            terminal,
            width,
            height,
            resize_pending: false,
            should_quit: false,
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

            let pixels = self.renderer.render()?;
            let status = self.args.status_bar.then(|| status_line(delta_time));
            self.terminal
                .draw(&pixels, self.width, self.height, status.as_deref())?;

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
                Event::Key(key) if key.kind == KeyEventKind::Press => self.handle_key(key),
                Event::Resize(..) => self.resize_pending = true,
                _ => {}
            }
        }
        Ok(())
    }

    fn handle_key(&mut self, key: KeyEvent) {
        match key.code {
            KeyCode::Char('q') => self.should_quit = true,
            KeyCode::Char('c') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                self.should_quit = true;
            }
            _ => {}
        }
    }

    fn resize(&mut self) -> Result<(), Box<dyn Error>> {
        self.resize_pending = false;
        let (width, height) = terminal::render_size(&self.args)?;
        if (width, height) != (self.width, self.height) {
            self.width = width;
            self.height = height;
            self.renderer.resize(width, height);
        }
        // The terminal may have shrunk even if the render size is fixed
        self.terminal.clear()?;
        Ok(())
    }
}

fn status_line(delta_time: f32) -> String {
    let fps = if delta_time > 0.0 {
        1.0 / delta_time
    } else {
        0.0
    };
    format!(" FPS: {fps:.1} ")
}
