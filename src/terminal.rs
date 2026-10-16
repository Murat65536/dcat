use crossterm::{
    QueueableCommand, SynchronizedUpdate,
    cursor::{Hide, MoveTo, Show},
    execute, queue,
    style::{Color, Colors, Print, SetColors},
    terminal::{EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode},
};
use std::io::{self, Stdout, stdout};

pub struct Terminal {
    stdout: Stdout,
}

impl Terminal {
    pub fn new() -> io::Result<Self> {
        let mut stdout = stdout();
        execute!(stdout, EnterAlternateScreen, Hide)?;
        enable_raw_mode()?;
        Ok(Self { stdout })
    }

    pub fn draw(&mut self, pixels: Vec<u8>, width: u32, height: u32) -> io::Result<()> {
        self.stdout.sync_update(|stdout| -> io::Result<()> {
            stdout.queue(MoveTo(0, 0))?;
            for row in (0..height).step_by(2) {
                for col in 0..width {
                    let start_top = (row as usize * width as usize + col as usize) * 3;
                    let foreground = Color::Rgb {
                        r: pixels[start_top],
                        g: pixels[start_top + 1],
                        b: pixels[start_top + 2],
                    };

                    let start_bottom = ((row as usize + 1) * width as usize + col as usize) * 3;
                    let background = Color::Rgb {
                        r: pixels[start_bottom],
                        g: pixels[start_bottom + 1],
                        b: pixels[start_bottom + 2],
                    };

                    queue!(
                        stdout,
                        SetColors(Colors::new(foreground, background)),
                        Print("▀")
                    )?;
                }
            }
            Ok(())
        })??;
        Ok(())
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = execute!(self.stdout, Show, LeaveAlternateScreen);
    }
}
