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
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::{Receiver, SyncSender, channel, sync_channel},
    },
    thread::{self, JoinHandle},
};

use crate::{
    cli::{Args, OutputMode},
    output::{self, ColorDepth, Frame},
    profile,
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
    reports_key_releases: bool,
    // None once stopped, which only happens on drop or after a write error
    writer: Option<FrameWriter>,
}

enum WriterJob {
    Frame {
        // Tightly packed RGBA
        pixels: Vec<u8>,
        width: u32,
        height: u32,
        status: Option<String>,
        // Terminal size in cells and cell size in pixels when the frame was rendered
        size: (u16, u16),
        cell_pixels: (u32, u32),
    },
    Clear,
}

// Encodes and writes frames on a background thread, so encoding one frame and
// the terminal reading it overlap rendering the next. The job channel holds
// nothing: sending waits until the writer is free, so at most one frame is in
// progress behind the one being rendered.
struct FrameWriter {
    jobs: SyncSender<WriterJob>,
    // Pixel buffers handed back for reuse, so big frames aren't reallocated
    recycled: Receiver<Vec<u8>>,
    thread: JoinHandle<io::Result<()>>,
}

impl FrameWriter {
    fn spawn(mode: OutputMode) -> io::Result<Self> {
        let (jobs, job_receiver) = sync_channel::<WriterJob>(0);
        let (recycle, recycled) = channel();
        let thread = thread::Builder::new()
            .name("frame writer".into())
            .spawn(move || {
                profiling::register_thread!();
                let mut buffer = Vec::new();
                for job in job_receiver {
                    buffer.clear();
                    {
                        profiling::scope!("Encode");
                        match job {
                            WriterJob::Clear => queue!(buffer, Clear(ClearType::All))?,
                            WriterJob::Frame {
                                pixels,
                                width,
                                height,
                                status,
                                size,
                                cell_pixels,
                            } => {
                                let frame = Frame {
                                    width,
                                    height,
                                    pixels: &pixels,
                                };
                                compose_frame(
                                    mode,
                                    cell_pixels,
                                    frame,
                                    status.as_deref(),
                                    size,
                                    &mut buffer,
                                )?;
                                // Fails only if the terminal is being dropped
                                let _ = recycle.send(pixels);
                            }
                        }
                    }
                    profile::plot!("Output bytes", buffer.len());

                    profiling::scope!("Write");
                    let mut stdout = stdout().lock();
                    stdout.write_all(&buffer)?;
                    stdout.flush()?;
                }
                Ok(())
            })?;
        Ok(Self {
            jobs,
            recycled,
            thread,
        })
    }
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
            reports_key_releases,
            writer: Some(FrameWriter::spawn(mode)?),
        })
    }

    pub fn reports_key_releases(&self) -> bool {
        self.reports_key_releases
    }

    // Clear leftovers from a larger previous frame
    pub fn clear(&mut self) -> io::Result<()> {
        self.send(WriterJob::Clear)
    }

    // A buffer for the next frame's pixels, reusing one already written if available
    pub fn take_buffer(&mut self) -> Vec<u8> {
        self.writer
            .as_ref()
            .and_then(|writer| writer.recycled.try_recv().ok())
            .unwrap_or_default()
    }

    // Queue a frame of tightly packed RGBA pixels for display
    pub fn draw(
        &mut self,
        pixels: Vec<u8>,
        width: u32,
        height: u32,
        status: Option<String>,
    ) -> io::Result<()> {
        let job = WriterJob::Frame {
            pixels,
            width,
            height,
            status,
            size: terminal::size()?,
            cell_pixels: cell_pixels(self.mode),
        };
        self.send(job)
    }

    fn send(&mut self, job: WriterJob) -> io::Result<()> {
        let sent = match &self.writer {
            Some(writer) => writer.jobs.send(job).is_ok(),
            None => false,
        };
        if sent {
            return Ok(());
        }
        // The writer only stops early when writing failed; report why
        self.stop_writer()?;
        Err(io::Error::other("frame writer stopped"))
    }

    // Wait for queued output to be written, returning the writer's error if it failed
    fn stop_writer(&mut self) -> io::Result<()> {
        let Some(writer) = self.writer.take() else {
            return Ok(());
        };
        drop(writer.jobs);
        writer
            .thread
            .join()
            .unwrap_or_else(|_| Err(io::Error::other("frame writer panicked")))
    }
}

// Everything written to the terminal for one frame: the image in the given
// mode and the status bar, fitted to a terminal of `cols` by `rows` cells
fn compose_frame(
    mode: OutputMode,
    (cell_width, cell_height): (u32, u32),
    frame: Frame,
    status: Option<&str>,
    (cols, rows): (u16, u16),
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let image_rows = rows.saturating_sub(reserved_rows(mode, status.is_some()));
    let (cols_used, rows_used) = (usize::from(cols), usize::from(image_rows));

    queue!(out, BeginSynchronizedUpdate)?;

    match mode {
        OutputMode::TruecolorCharacters => {
            output::encode_half_blocks(frame, cols_used, rows_used, ColorDepth::Truecolor, out)?;
        }
        OutputMode::PaletteCharacters => {
            output::encode_half_blocks(frame, cols_used, rows_used, ColorDepth::Palette, out)?;
        }
        OutputMode::BlockCharacters => {
            output::encode_mono_blocks(frame, cols_used, rows_used, out)?;
        }
        OutputMode::Kitty | OutputMode::Sixel => {
            // Crop anything that wouldn't fit on screen
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
                if mode == OutputMode::Kitty {
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

    queue!(out, EndSynchronizedUpdate)
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
        // Finish writing before restoring, so the two never interleave
        let _ = self.stop_writer();
        restore();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::num::NonZeroU16;
    use vt100::Color as Vt;

    const CELL_PIXELS: (u32, u32) = (10, 20);

    // Feed a composed frame through a terminal emulator of the given size
    fn screen(
        mode: OutputMode,
        frame: Frame,
        status: Option<&str>,
        (cols, rows): (u16, u16),
    ) -> vt100::Parser {
        let mut out = Vec::new();
        compose_frame(mode, CELL_PIXELS, frame, status, (cols, rows), &mut out).unwrap();
        let mut parser = vt100::Parser::new(
            NonZeroU16::new(rows).unwrap(),
            NonZeroU16::new(cols).unwrap(),
            0,
        );
        parser.process(&out);
        parser
    }

    // A different color for every pixel, so misplaced pixels show up
    fn gradient(width: u32, height: u32) -> Vec<u8> {
        (0..height)
            .flat_map(|y| (0..width).flat_map(move |x| [(x * 20) as u8, (y * 20) as u8, 100, 255]))
            .collect()
    }

    fn pixel(pixels: &[u8], width: u32, x: u16, y: u16) -> [u8; 3] {
        let start = (usize::from(y) * width as usize + usize::from(x)) * 4;
        [pixels[start], pixels[start + 1], pixels[start + 2]]
    }

    fn rgb([r, g, b]: [u8; 3]) -> Vt {
        Vt::Rgb(r, g, b)
    }

    fn row_text(parser: &vt100::Parser, row: u16, cols: u16) -> String {
        (0..cols)
            .map(|col| parser.screen().cell(row, col).unwrap().contents())
            .collect()
    }

    #[test]
    fn truecolor_cells_hold_two_pixels() {
        let pixels = gradient(4, 6);
        let frame = Frame {
            width: 4,
            height: 6,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (4, 3));

        for row in 0..3 {
            for col in 0..4 {
                let cell = parser.screen().cell(row, col).unwrap();
                assert_eq!(cell.contents(), "▀", "({row}, {col})");
                assert_eq!(cell.fgcolor(), rgb(pixel(&pixels, 4, col, row * 2)));
                assert_eq!(cell.bgcolor(), rgb(pixel(&pixels, 4, col, row * 2 + 1)));
            }
        }
    }

    #[test]
    fn palette_cells_use_palette_colors() {
        let pixels = gradient(3, 2);
        let frame = Frame {
            width: 3,
            height: 2,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::PaletteCharacters, frame, None, (3, 1));

        for col in 0..3 {
            let cell = parser.screen().cell(0, col).unwrap();
            let expected = |y| Vt::Idx(output::nearest_palette_index(pixel(&pixels, 3, col, y)));
            assert_eq!(cell.fgcolor(), expected(0));
            assert_eq!(cell.bgcolor(), expected(1));
        }
    }

    #[test]
    fn mono_blocks_use_terminal_colors() {
        let white = [255u8; 4];
        let black = [0, 0, 0, 255];
        let pixels = [white, black, black, white].concat();
        let frame = Frame {
            width: 2,
            height: 2,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::BlockCharacters, frame, None, (2, 1));

        assert_eq!(row_text(&parser, 0, 2), "▀▄");
        for col in 0..2 {
            let cell = parser.screen().cell(0, col).unwrap();
            assert_eq!((cell.fgcolor(), cell.bgcolor()), (Vt::Default, Vt::Default));
        }
    }

    #[test]
    fn status_bar_takes_the_last_row() {
        let pixels = gradient(4, 6);
        let frame = Frame {
            width: 4,
            height: 6,
            pixels: &pixels,
        };
        let parser = screen(
            OutputMode::TruecolorCharacters,
            frame,
            Some("FPS: 60.0"),
            (4, 3),
        );

        // The image keeps the rows above, and the status is cut to the width
        assert_eq!(row_text(&parser, 1, 4), "▀▀▀▀");
        assert_eq!(row_text(&parser, 2, 4), "FPS:");
        assert!(parser.screen().cell(2, 0).unwrap().inverse());
        assert!(!parser.screen().cell(1, 0).unwrap().inverse());
    }

    #[test]
    fn oversized_frames_are_clipped_without_scrolling() {
        let pixels = gradient(10, 10);
        let frame = Frame {
            width: 10,
            height: 10,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (4, 3));

        // Had anything wrapped or scrolled, the corners would hold other pixels
        let top_left = parser.screen().cell(0, 0).unwrap();
        assert_eq!(top_left.fgcolor(), rgb(pixel(&pixels, 10, 0, 0)));
        let bottom_right = parser.screen().cell(2, 3).unwrap();
        assert_eq!(bottom_right.fgcolor(), rgb(pixel(&pixels, 10, 3, 4)));
        assert_eq!(bottom_right.bgcolor(), rgb(pixel(&pixels, 10, 3, 5)));
        assert_eq!(parser.screen().cursor_position().0, 2);
    }

    #[test]
    fn odd_height_leaves_the_last_half_cell_empty() {
        let pixels = gradient(2, 3);
        let frame = Frame {
            width: 2,
            height: 3,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (2, 2));

        let cell = parser.screen().cell(1, 1).unwrap();
        assert_eq!(cell.fgcolor(), rgb(pixel(&pixels, 2, 1, 2)));
        assert_eq!(cell.bgcolor(), Vt::Default);
    }

    #[test]
    fn image_protocols_leave_no_text_behind() {
        // A frame larger than the screen, so cropping is exercised too
        let pixels = gradient(50, 70);
        let frame = Frame {
            width: 50,
            height: 70,
            pixels: &pixels,
        };
        for mode in [OutputMode::Kitty, OutputMode::Sixel] {
            let parser = screen(mode, frame, Some("status"), (4, 3));
            for row in 0..2 {
                assert_eq!(row_text(&parser, row, 4).trim(), "", "{mode:?} row {row}");
            }
            assert_eq!(row_text(&parser, 2, 4), "stat", "{mode:?}");
        }
    }

    #[test]
    fn sixel_keeps_a_spare_row() {
        assert_eq!(reserved_rows(OutputMode::Sixel, false), 1);
        assert_eq!(reserved_rows(OutputMode::Kitty, false), 0);
        assert_eq!(reserved_rows(OutputMode::TruecolorCharacters, true), 1);
    }
}
