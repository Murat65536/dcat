use crossterm::{
    cursor::{Hide, MoveTo, Show},
    event::{
        DisableMouseCapture, EnableMouseCapture, KeyboardEnhancementFlags,
        PopKeyboardEnhancementFlags, PushKeyboardEnhancementFlags,
    },
    execute, queue,
    style::{Attribute, Print, SetAttribute},
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

use crate::{
    cli::{Args, OutputMode},
    output::{self, ColorDepth, Frame},
};

// Source pixels covered by one character cell in the half-block modes
const CHARACTER_CELL_PIXELS: (u32, u32) = (1, 2);
// Assumed cell size when the terminal doesn't report its size in pixels
const FALLBACK_CELL_PIXELS: (u32, u32) = (10, 20);

// Report mouse positions in pixels rather than cells (SGR-Pixels)
const ENABLE_PIXEL_MOUSE: &str = "\x1b[?1016h";
const DISABLE_PIXEL_MOUSE: &str = "\x1b[?1016l";

// Terminal modes to undo on exit, readable from the panic hook
static MOUSE_CAPTURED: AtomicBool = AtomicBool::new(false);
static KEYBOARD_ENHANCED: AtomicBool = AtomicBool::new(false);
static KITTY_IMAGES_SHOWN: AtomicBool = AtomicBool::new(false);
static SIXEL_SCROLLING_DISABLED: AtomicBool = AtomicBool::new(false);

// Pixels covered by one character cell
fn cell_pixels(mode: OutputMode) -> (u32, u32) {
    if mode.uses_characters() {
        return CHARACTER_CELL_PIXELS;
    }
    match terminal::window_size() {
        Ok(size) if size.width > 0 && size.height > 0 && size.columns > 0 && size.rows > 0 => (
            (u32::from(size.width) / u32::from(size.columns)).max(1),
            (u32::from(size.height) / u32::from(size.rows)).max(1),
        ),
        _ => FALLBACK_CELL_PIXELS,
    }
}

// Rows below the image: the status bar, and for Sixel a spare row so the
// image never reaches the bottom of the screen and scrolls it
fn reserved_rows(mode: OutputMode, status_bar: bool) -> u16 {
    u16::from(status_bar || mode == OutputMode::Sixel)
}

// Pixel size to render at: the explicit size if given, otherwise what fits
// in the terminal above the reserved rows
pub fn render_size(args: &Args, mode: OutputMode) -> io::Result<(u32, u32)> {
    if let (Some(width), Some(height)) = (args.width, args.height) {
        return Ok((width, height));
    }

    let (cols, rows) = terminal::size()?;
    let image_rows = rows.saturating_sub(reserved_rows(mode, args.status_bar));
    let (cell_width, cell_height) = cell_pixels(mode);
    Ok((
        u32::from(cols.max(1)) * cell_width,
        u32::from(image_rows.max(1)) * cell_height,
    ))
}

pub struct Terminal {
    mode: OutputMode,
    buffer: Vec<u8>,
    reports_key_releases: bool,
}

impl Terminal {
    // `key_releases` asks for key release events, which first-person movement needs
    pub fn new(mode: OutputMode, mouse_capture: bool, key_releases: bool) -> io::Result<Self> {
        // Restore the terminal before the panic message is printed, otherwise
        // it is lost on the alternate screen.
        let default_hook = panic::take_hook();
        panic::set_hook(Box::new(move |info| {
            restore();
            default_hook(info);
        }));

        execute!(stdout(), EnterAlternateScreen, Hide)?;
        enable_raw_mode()?;

        match mode {
            OutputMode::Kitty => KITTY_IMAGES_SHOWN.store(true, Ordering::SeqCst),
            OutputMode::Sixel => {
                execute!(stdout(), Print(output::SIXEL_DISABLE_SCROLLING))?;
                SIXEL_SCROLLING_DISABLED.store(true, Ordering::SeqCst);
            }
            _ => {}
        }

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
            mode,
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

    pub fn draw(&mut self, frame: Frame, status: Option<&str>) -> io::Result<()> {
        let (cols, rows) = terminal::size()?;
        let image_rows = rows.saturating_sub(reserved_rows(self.mode, status.is_some()));
        let (cols_used, rows_used) = (usize::from(cols), usize::from(image_rows));

        let out = &mut self.buffer;
        out.clear();
        queue!(out, BeginSynchronizedUpdate)?;

        match self.mode {
            OutputMode::TruecolorCharacters => {
                output::encode_half_blocks(
                    frame,
                    cols_used,
                    rows_used,
                    ColorDepth::Truecolor,
                    out,
                )?;
            }
            OutputMode::PaletteCharacters => {
                output::encode_half_blocks(frame, cols_used, rows_used, ColorDepth::Palette, out)?;
            }
            OutputMode::BlockCharacters => {
                output::encode_mono_blocks(frame, cols_used, rows_used, out)?;
            }
            OutputMode::Kitty | OutputMode::Sixel => {
                // Crop anything that wouldn't fit on screen
                let (cell_width, cell_height) = cell_pixels(self.mode);
                let max_width = u32::from(cols) * cell_width;
                let max_height = u32::from(image_rows) * cell_height;
                let cropped;
                let frame = if frame.width > max_width || frame.height > max_height {
                    cropped = frame.cropped(max_width, max_height);
                    Frame {
                        width: frame.width.min(max_width),
                        height: frame.height.min(max_height),
                        pixels: &cropped,
                    }
                } else {
                    frame
                };

                queue!(out, MoveTo(0, 0))?;
                if frame.width > 0 && frame.height > 0 {
                    if self.mode == OutputMode::Kitty {
                        output::encode_kitty(frame, out)?;
                    } else {
                        output::encode_sixel(frame, out)?;
                    }
                }
            }
        }

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
    if KITTY_IMAGES_SHOWN.swap(false, Ordering::SeqCst) {
        let _ = execute!(stdout(), Print(output::KITTY_DELETE_ALL));
    }
    if SIXEL_SCROLLING_DISABLED.swap(false, Ordering::SeqCst) {
        let _ = execute!(stdout(), Print(output::SIXEL_ENABLE_SCROLLING));
    }
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
