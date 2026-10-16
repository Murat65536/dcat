use crossterm::{
    cursor::{Hide, MoveTo, Show},
    event::{
        DisableMouseCapture, EnableMouseCapture, KeyboardEnhancementFlags,
        PopKeyboardEnhancementFlags, PushKeyboardEnhancementFlags,
    },
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
    sync::atomic::{AtomicBool, Ordering},
};

use crate::cli::Args;

// Source pixels covered by one character cell in half-block mode
const CELL_WIDTH: u32 = 1;
const CELL_HEIGHT: u32 = 2;

// Report mouse positions in pixels rather than cells (SGR-Pixels)
const ENABLE_PIXEL_MOUSE: &str = "\x1b[?1016h";
const DISABLE_PIXEL_MOUSE: &str = "\x1b[?1016l";

// Terminal modes to undo on exit, readable from the panic hook
static MOUSE_CAPTURED: AtomicBool = AtomicBool::new(false);
static KEYBOARD_ENHANCED: AtomicBool = AtomicBool::new(false);

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
    reports_key_releases: bool,
}

impl Terminal {
    // `key_releases` asks for key release events, which first-person movement needs
    pub fn new(mouse_capture: bool, key_releases: bool) -> io::Result<Self> {
        // Restore the terminal before the panic message is printed, otherwise
        // it is lost on the alternate screen.
        let default_hook = panic::take_hook();
        panic::set_hook(Box::new(move |info| {
            restore();
            default_hook(info);
        }));

        execute!(stdout(), EnterAlternateScreen, Hide)?;
        enable_raw_mode()?;

        if mouse_capture {
            execute!(stdout(), EnableMouseCapture)?;
            if cfg!(unix) {
                execute!(stdout(), Print(ENABLE_PIXEL_MOUSE))?;
            }
            MOUSE_CAPTURED.store(true, Ordering::SeqCst);
        }

        // The Windows console reports releases natively; elsewhere they need
        // the Kitty keyboard protocol. A terminal that doesn't answer the
        // query is treated as not supporting it.
        let mut reports_key_releases = cfg!(windows);
        if key_releases
            && !reports_key_releases
            && terminal::supports_keyboard_enhancement().unwrap_or(false)
        {
            execute!(
                stdout(),
                PushKeyboardEnhancementFlags(
                    KeyboardEnhancementFlags::DISAMBIGUATE_ESCAPE_CODES
                        | KeyboardEnhancementFlags::REPORT_EVENT_TYPES
                        | KeyboardEnhancementFlags::REPORT_ALL_KEYS_AS_ESCAPE_CODES
                )
            )?;
            KEYBOARD_ENHANCED.store(true, Ordering::SeqCst);
            reports_key_releases = true;
        }

        Ok(Self {
            buffer: Vec::new(),
            reports_key_releases,
        })
    }

    pub fn reports_key_releases(&self) -> bool {
        self.reports_key_releases
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
    if KEYBOARD_ENHANCED.swap(false, Ordering::SeqCst) {
        let _ = execute!(stdout(), PopKeyboardEnhancementFlags);
    }
    if MOUSE_CAPTURED.swap(false, Ordering::SeqCst) {
        if cfg!(unix) {
            let _ = execute!(stdout(), Print(DISABLE_PIXEL_MOUSE));
        }
        let _ = execute!(stdout(), DisableMouseCapture);
    }
    let _ = disable_raw_mode();
    let _ = execute!(stdout(), Show, LeaveAlternateScreen);
}

impl Drop for Terminal {
    fn drop(&mut self) {
        restore();
    }
}
