use crossterm::{
    cursor::{Hide, MoveTo, Show},
    execute, queue,
    style::{Attribute, Color, Colors, Print, ResetColor, SetAttribute, SetColors},
    terminal::{
        self, BeginSynchronizedUpdate, Clear, ClearType, EndSynchronizedUpdate,
        EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode,
    },
};
use std::{
    io::{self, Write, stdout},
    panic,
};

use crate::cli::Args;

// Source pixels covered by one character cell in half-block mode
const CELL_WIDTH: u32 = 1;
const CELL_HEIGHT: u32 = 2;

// Pixel size to render at: the explicit size if given, otherwise the terminal
// size minus the status bar line.
pub fn render_size(args: &Args) -> io::Result<(u32, u32)> {
    if let (Some(width), Some(height)) = (args.width, args.height) {
        return Ok((width, height));
    }

    let (cols, rows) = terminal::size()?;
    let rows = if args.status_bar {
        rows.saturating_sub(1)
    } else {
        rows
    };
    Ok((
        u32::from(cols.max(1)) * CELL_WIDTH,
        u32::from(rows.max(1)) * CELL_HEIGHT,
    ))
}

pub struct Terminal {
    buffer: Vec<u8>,
}

impl Terminal {
    pub fn new() -> io::Result<Self> {
        // Restore the terminal before the panic message is printed, otherwise
        // it is lost on the alternate screen.
        let default_hook = panic::take_hook();
        panic::set_hook(Box::new(move |info| {
            restore();
            default_hook(info);
        }));

        execute!(stdout(), EnterAlternateScreen, Hide)?;
        enable_raw_mode()?;
        Ok(Self { buffer: Vec::new() })
    }

    // Clear leftovers from a larger previous frame
    pub fn clear(&mut self) -> io::Result<()> {
        execute!(stdout(), Clear(ClearType::All))
    }

    pub fn draw(
        &mut self,
        pixels: &[u8],
        width: u32,
        height: u32,
        status: Option<&str>,
    ) -> io::Result<()> {
        let (cols, rows) = terminal::size()?;
        let image_rows = if status.is_some() {
            rows.saturating_sub(1)
        } else {
            rows
        };
        let visible_cols = (width / CELL_WIDTH).min(u32::from(cols)) as usize;
        let visible_rows = height.div_ceil(CELL_HEIGHT).min(u32::from(image_rows)) as usize;
        let width = width as usize;
        let height = height as usize;

        let pixel = |x: usize, y: usize| {
            let start = (y * width + x) * 3;
            Color::Rgb {
                r: pixels[start],
                g: pixels[start + 1],
                b: pixels[start + 2],
            }
        };

        let out = &mut self.buffer;
        out.clear();
        queue!(out, BeginSynchronizedUpdate)?;

        for row in 0..visible_rows {
            queue!(out, MoveTo(0, row as u16))?;
            let top = row * 2;
            let bottom = top + 1;
            for col in 0..visible_cols {
                let foreground = pixel(col, top);
                // An odd height leaves the last cell without a bottom pixel
                let background = if bottom < height {
                    pixel(col, bottom)
                } else {
                    Color::Reset
                };
                queue!(
                    out,
                    SetColors(Colors::new(foreground, background)),
                    Print('▀')
                )?;
            }
        }
        queue!(out, ResetColor)?;

        if let Some(status) = status
            && rows > 0
        {
            let status: String = status.chars().take(usize::from(cols)).collect();
            queue!(
                out,
                MoveTo(0, rows - 1),
                Clear(ClearType::CurrentLine),
                SetAttribute(Attribute::Reverse),
                Print(status),
                SetAttribute(Attribute::Reset)
            )?;
        }

        queue!(out, EndSynchronizedUpdate)?;

        let mut stdout = stdout().lock();
        stdout.write_all(out)?;
        stdout.flush()
    }
}

fn restore() {
    let _ = disable_raw_mode();
    let _ = execute!(stdout(), Show, LeaveAlternateScreen);
}

impl Drop for Terminal {
    fn drop(&mut self) {
        restore();
    }
}
