use crossterm::{
    cursor::{Hide, MoveTo, Show},
    event::{
        DisableFocusChange, DisableMouseCapture, EnableFocusChange, EnableMouseCapture,
        KeyboardEnhancementFlags, PopKeyboardEnhancementFlags, PushKeyboardEnhancementFlags,
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
        atomic::{AtomicBool, AtomicU32, Ordering},
        mpsc::{Receiver, SyncSender, channel, sync_channel},
    },
    thread::{self, JoinHandle},
};

use crate::{
    cli::{Args, OutputMode},
    output::{self, ColorDepth, Frame, KittyPlacement},
    profile,
};

// Source pixels covered by one character cell in the character modes
const CHARACTER_CELL_PIXELS: (u32, u32) = (output::CELL_COLUMNS as u32, output::CELL_ROWS as u32);
// Assumed cell size when the terminal doesn't report its size in pixels
const FALLBACK_CELL_PIXELS: (u32, u32) = (10, 20);

// Report mouse positions in pixels rather than cells (SGR-Pixels)
const ENABLE_PIXEL_MOUSE: &str = "\x1b[?1016h";
const DISABLE_PIXEL_MOUSE: &str = "\x1b[?1016l";

// Terminal modes to undo on exit, readable from the panic hook
static MOUSE_CAPTURED: AtomicBool = AtomicBool::new(false);
static KEYBOARD_ENHANCED: AtomicBool = AtomicBool::new(false);
static FOCUS_REPORTED: AtomicBool = AtomicBool::new(false);
// The id of the Kitty image dcat shows, or 0 for none
static KITTY_IMAGE_SHOWN: AtomicU32 = AtomicU32::new(0);
// Whether Kitty commands have to be passed through tmux
static KITTY_IN_TMUX: AtomicBool = AtomicBool::new(false);
static SIXEL_SCROLLING_DISABLED: AtomicBool = AtomicBool::new(false);

// Pixels covered by one character cell. A given size wins over the terminal's,
// for ptys that report none, like the ones Neovim jobs run in.
fn cell_pixels(mode: OutputMode, cell_size: Option<(u32, u32)>) -> (u32, u32) {
    if mode.uses_characters() {
        return CHARACTER_CELL_PIXELS;
    }
    if let Some(size) = cell_size {
        return size;
    }
    match terminal::window_size() {
        Ok(size) if size.width > 0 && size.height > 0 && size.columns > 0 && size.rows > 0 => (
            (u32::from(size.width) / u32::from(size.columns)).max(1),
            (u32::from(size.height) / u32::from(size.rows)).max(1),
        ),
        _ => FALLBACK_CELL_PIXELS,
    }
}

// Whether the terminal says it can read frames from shared memory
fn terminal_reads_shared_memory() -> bool {
    #[cfg(unix)]
    let supported = crate::shm::terminal_supports();
    #[cfg(not(unix))]
    let supported = false;
    supported
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
    let (cell_width, cell_height) = cell_pixels(mode, args.cell_size);
    Ok((
        u32::from(cols.max(1)) * cell_width,
        u32::from(image_rows.max(1)) * cell_height,
    ))
}

pub struct Terminal {
    mode: OutputMode,
    cell_size: Option<(u32, u32)>,
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

// Whether Kitty frames go through shared memory
#[derive(Clone, Copy, PartialEq)]
enum SharedMemory {
    Off,
    // The terminal said it reads it, or likely does. If it stops, frames go
    // inline instead.
    Likely,
    // The caller says the terminal reads it, but frames may be skipped, like
    // dcat.nvim does while its viewer is hidden. Unread ones are removed.
    Assumed,
}

impl FrameWriter {
    fn spawn(
        mode: OutputMode,
        kitty_image_id: u32,
        shared_memory: SharedMemory,
        tmux: bool,
    ) -> io::Result<Self> {
        let (jobs, job_receiver) = sync_channel::<WriterJob>(0);
        let (recycle, recycled) = channel();
        let thread = thread::Builder::new()
            .name("frame writer".into())
            .spawn(move || {
                profiling::register_thread!();
                let mut buffer = Vec::new();
                #[cfg(unix)]
                let mut shared_frames = (shared_memory != SharedMemory::Off)
                    .then(|| crate::shm::SharedFrames::new(shared_memory == SharedMemory::Assumed));
                #[cfg(not(unix))]
                let _ = shared_memory;
                let mut placeholders_drawn = None;
                for job in job_receiver {
                    buffer.clear();
                    {
                        profiling::scope!("Encode");
                        match job {
                            WriterJob::Clear => {
                                queue!(buffer, Clear(ClearType::All))?;
                                placeholders_drawn = None;
                            }
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
                                #[cfg(unix)]
                                let mut write_shared = shared_frames
                                    .as_mut()
                                    .map(|frames| move |data: &[u8]| frames.write(data));
                                let kitty = KittyImage {
                                    id: kitty_image_id,
                                    #[cfg(unix)]
                                    write_shared: write_shared
                                        .as_mut()
                                        .map(|write| write as &mut WriteShared<'_>),
                                    #[cfg(not(unix))]
                                    write_shared: None,
                                    placeholders_drawn: tmux.then_some(&mut placeholders_drawn),
                                };
                                compose_frame(
                                    mode,
                                    cell_pixels,
                                    frame,
                                    status.as_deref(),
                                    size,
                                    kitty,
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
    pub fn new(mode: OutputMode, args: &Args) -> io::Result<Self> {
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
            OutputMode::Kitty => KITTY_IMAGE_SHOWN.store(args.kitty_image_id, Ordering::SeqCst),
            OutputMode::Sixel => {
                execute!(stdout(), Print(output::SIXEL_DISABLE_SCROLLING))?;
                SIXEL_SCROLLING_DISABLED.store(true, Ordering::SeqCst);
            }
            _ => {}
        }

        if args.mouse_orbit {
            execute!(stdout(), EnableMouseCapture)?;
            if cfg!(unix) {
                execute!(stdout(), Print(ENABLE_PIXEL_MOUSE))?;
            }
            MOUSE_CAPTURED.store(true, Ordering::SeqCst);
        }

        // Kitty can read frames from shared memory when it runs on this
        // machine. The query reads the reply itself, so it must come before
        // anything else reads input. It can be skipped when the caller knows
        // better, like inside Neovim, whose terminal answers part of it itself.
        // Inside tmux, Kitty commands have to be passed through it
        let tmux = mode == OutputMode::Kitty && std::env::var_os("TMUX").is_some();
        KITTY_IN_TMUX.store(tmux, Ordering::SeqCst);
        let over_ssh = ["SSH_CONNECTION", "SSH_TTY"]
            .iter()
            .any(|name| std::env::var_os(name).is_some());
        let shared_memory = if mode != OutputMode::Kitty {
            SharedMemory::Off
        } else if args.kitty_shared_memory {
            SharedMemory::Assumed
        } else if tmux {
            // tmux would take the query for a window title, and shows Kitty its
            // own answer. Shared memory is tried anyway unless over SSH, since
            // big inline frames back up in tmux until it drops some.
            if over_ssh {
                SharedMemory::Off
            } else {
                SharedMemory::Likely
            }
        } else if terminal_reads_shared_memory() {
            SharedMemory::Likely
        } else {
            SharedMemory::Off
        };

        // First-person movement needs key releases. The Windows console reports
        // them natively; elsewhere they need the Kitty keyboard protocol. A
        // terminal that doesn't answer the query is treated as not supporting it.
        // Neovim's terminal answers it, but never gets releases itself from the
        // terminal it runs in, so keys would stay held.
        let mut reports_key_releases = cfg!(windows);
        if args.keyboard_controls
            && !reports_key_releases
            && std::env::var_os("NVIM").is_none()
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
            cell_size: args.cell_size,
            reports_key_releases,
            writer: Some(FrameWriter::spawn(
                mode,
                args.kitty_image_id,
                shared_memory,
                tmux,
            )?),
        })
    }

    pub fn reports_key_releases(&self) -> bool {
        self.reports_key_releases
    }

    // Ask the terminal to report gaining and losing focus
    pub fn report_focus(&mut self) -> io::Result<()> {
        execute!(stdout(), EnableFocusChange)?;
        FOCUS_REPORTED.store(true, Ordering::SeqCst);
        Ok(())
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
            cell_pixels: cell_pixels(self.mode, self.cell_size),
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
// Writes a frame's pixels to shared memory and returns the object's name
type WriteShared<'a> = dyn FnMut(&[u8]) -> io::Result<String> + 'a;

// How Kitty frames are sent
struct KittyImage<'a> {
    id: u32,
    // None to send the pixels inline
    write_shared: Option<&'a mut WriteShared<'a>>,
    // Inside tmux, the size of the placeholder text last drawn. tmux keeps
    // it, so it's only drawn again when that changes.
    placeholders_drawn: Option<&'a mut Option<(u16, u16)>>,
}

fn compose_frame(
    mode: OutputMode,
    (cell_width, cell_height): (u32, u32),
    frame: Frame,
    status: Option<&str>,
    (cols, rows): (u16, u16),
    kitty: KittyImage<'_>,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let image_rows = rows.saturating_sub(reserved_rows(mode, status.is_some()));
    let (cols_used, rows_used) = (usize::from(cols), usize::from(image_rows));

    // tmux redraws a whole pane after a synchronized update, which would send
    // the placeholder text again every frame. Kitty swaps images at once anyway.
    let synchronized = kitty.placeholders_drawn.is_none();
    if synchronized {
        queue!(out, BeginSynchronizedUpdate)?;
    }

    match mode {
        OutputMode::TruecolorCharacters => {
            output::encode_symbols(frame, cols_used, rows_used, ColorDepth::Truecolor, out)?;
        }
        OutputMode::PaletteCharacters => {
            output::encode_symbols(frame, cols_used, rows_used, ColorDepth::Palette, out)?;
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
                    // Inside tmux the image goes in a virtual placement, which
                    // placeholder text shows
                    let cells = (
                        frame.width.div_ceil(cell_width) as u16,
                        frame.height.div_ceil(cell_height) as u16,
                    );
                    let placement = KittyPlacement {
                        image_id: kitty.id,
                        virtual_cells: kitty.placeholders_drawn.is_some().then_some(cells),
                    };
                    let start = out.len();
                    match kitty.write_shared.map(|write| write(frame.pixels)) {
                        Some(Ok(name)) => {
                            output::encode_kitty_shared(frame, placement, &name, out)?;
                        }
                        // Without shared memory, or if writing it failed,
                        // send the pixels in the escape sequence
                        _ => output::encode_kitty(frame, placement, out)?,
                    }
                    if let Some(drawn) = kitty.placeholders_drawn {
                        let commands = out.split_off(start);
                        output::wrap_for_tmux(&commands, out);
                        if *drawn != Some(cells) {
                            output::encode_kitty_placeholders(kitty.id, cells, out)?;
                            *drawn = Some(cells);
                        }
                    }
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

    if synchronized {
        queue!(out, EndSynchronizedUpdate)?;
    }
    Ok(())
}

fn restore() {
    let kitty_image = KITTY_IMAGE_SHOWN.swap(0, Ordering::SeqCst);
    if kitty_image != 0 {
        let delete = output::kitty_delete(kitty_image);
        let mut command = Vec::new();
        if KITTY_IN_TMUX.load(Ordering::SeqCst) {
            output::wrap_for_tmux(delete.as_bytes(), &mut command);
        } else {
            command.extend_from_slice(delete.as_bytes());
        }
        let _ = stdout().write_all(&command).and_then(|()| stdout().flush());
    }
    if SIXEL_SCROLLING_DISABLED.swap(false, Ordering::SeqCst) {
        let _ = execute!(stdout(), Print(output::SIXEL_ENABLE_SCROLLING));
    }
    if FOCUS_REPORTED.swap(false, Ordering::SeqCst) {
        let _ = execute!(stdout(), DisableFocusChange);
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
    use output::{CELL_COLUMNS, CELL_ROWS};
    use std::num::NonZeroU16;
    use vt100::Color as Vt;

    const PIXEL_CELL_SIZE: (u32, u32) = (10, 20);
    const SHAPES: &str = "▘▝▖▗▀▄▌▐▚▞▛▜▙▟▂▆";

    // Feed a composed frame through a terminal emulator of the given size
    fn screen(
        mode: OutputMode,
        frame: Frame,
        status: Option<&str>,
        (cols, rows): (u16, u16),
    ) -> vt100::Parser {
        let mut out = Vec::new();
        compose_frame(
            mode,
            PIXEL_CELL_SIZE,
            frame,
            status,
            (cols, rows),
            KittyImage {
                id: 1,
                write_shared: None,
                placeholders_drawn: None,
            },
            &mut out,
        )
        .unwrap();
        let mut parser = vt100::Parser::new(
            NonZeroU16::new(rows).unwrap(),
            NonZeroU16::new(cols).unwrap(),
            0,
        );
        parser.process(&out);
        parser
    }

    #[test]
    fn given_cell_size_wins_in_pixel_modes() {
        for mode in [OutputMode::Kitty, OutputMode::Sixel] {
            assert_eq!(cell_pixels(mode, Some((9, 18))), (9, 18), "{mode:?}");
        }
        // Character modes always sample the same pixels per cell
        assert_eq!(
            cell_pixels(OutputMode::TruecolorCharacters, Some((9, 18))),
            CHARACTER_CELL_PIXELS
        );
    }

    // RGBA pixels covering `cols` by `rows` cells. Each cell has two colors
    // laid out as a different block shape, so every cell can be shown exactly.
    fn shaped_frame(cols: usize, rows: usize) -> (u32, u32, Vec<u8>) {
        let (width, height) = (cols * CELL_COLUMNS, rows * CELL_ROWS);
        let shapes: Vec<char> = SHAPES.chars().collect();
        let mut pixels = vec![255; width * height * 4];
        for y in 0..height {
            for x in 0..width {
                let (col, row) = (x / CELL_COLUMNS, y / CELL_ROWS);
                let shape = shapes[(row * cols + col) % shapes.len()];
                let mask = output::symbol_mask(shape).unwrap();
                let bit = (y % CELL_ROWS) * CELL_COLUMNS + x % CELL_COLUMNS;
                let color = if mask & (1 << bit) != 0 {
                    [(col * 30) as u8, (row * 40) as u8, 200]
                } else {
                    [255 - (col * 30) as u8, 100, (row * 20) as u8]
                };
                let start = (y * width + x) * 4;
                pixels[start..start + 3].copy_from_slice(&color);
            }
        }
        (width as u32, height as u32, pixels)
    }

    // The source pixels of one cell
    fn source_cell(pixels: &[u8], width: u32, col: u16, row: u16) -> Vec<[u8; 3]> {
        (0..CELL_ROWS)
            .flat_map(|dy| (0..CELL_COLUMNS).map(move |dx| (dx, dy)))
            .map(|(dx, dy)| {
                let x = usize::from(col) * CELL_COLUMNS + dx;
                let y = usize::from(row) * CELL_ROWS + dy;
                let start = (y * width as usize + x) * 4;
                [pixels[start], pixels[start + 1], pixels[start + 2]]
            })
            .collect()
    }

    // The pixels a terminal cell shows, as the colors of its two regions
    fn shown_cell(parser: &vt100::Parser, col: u16, row: u16) -> Vec<Vt> {
        let cell = parser.screen().cell(row, col).unwrap();
        let symbol = cell.contents().chars().next().unwrap_or(' ');
        let mask = output::symbol_mask(symbol).unwrap_or_else(|| panic!("{symbol:?}"));
        (0..CELL_ROWS * CELL_COLUMNS)
            .map(|bit| {
                if mask & (1 << bit) != 0 {
                    cell.fgcolor()
                } else {
                    cell.bgcolor()
                }
            })
            .collect()
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
    fn truecolor_cells_reproduce_every_pixel() {
        let (width, height, pixels) = shaped_frame(4, 4);
        let frame = Frame {
            width,
            height,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (4, 4));

        for row in 0..4 {
            for col in 0..4 {
                let expected: Vec<Vt> = source_cell(&pixels, width, col, row)
                    .into_iter()
                    .map(rgb)
                    .collect();
                assert_eq!(shown_cell(&parser, col, row), expected, "({col}, {row})");
            }
        }
    }

    #[test]
    fn palette_cells_use_palette_colors() {
        let (width, height, pixels) = shaped_frame(3, 2);
        let frame = Frame {
            width,
            height,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::PaletteCharacters, frame, None, (3, 2));

        for row in 0..2 {
            for col in 0..3 {
                let expected: Vec<Vt> = source_cell(&pixels, width, col, row)
                    .into_iter()
                    .map(|color| Vt::Idx(ansi_colours::ansi256_from_rgb(color)))
                    .collect();
                assert_eq!(shown_cell(&parser, col, row), expected, "({col}, {row})");
            }
        }
    }

    #[test]
    fn mono_blocks_use_terminal_colors() {
        // Left cell lit on top, right cell lit on the bottom
        let (white, black) = ([255, 255, 255, 255], [0, 0, 0, 255]);
        let top = [white, white, black, black];
        let bottom = [black, black, white, white];
        let pixels: Vec<u8> = [top, top, bottom, bottom].concat().concat();
        let frame = Frame {
            width: 4,
            height: 4,
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
        let (width, height, pixels) = shaped_frame(4, 3);
        let frame = Frame {
            width,
            height,
            pixels: &pixels,
        };
        let parser = screen(
            OutputMode::TruecolorCharacters,
            frame,
            Some("FPS: 60.0"),
            (4, 3),
        );

        // The image keeps the rows above, and the status is cut to the width
        let image_row = row_text(&parser, 1, 4);
        assert_eq!(image_row.chars().count(), 4);
        assert!(
            image_row.chars().all(|c| SHAPES.contains(c)),
            "{image_row:?}"
        );
        assert_eq!(row_text(&parser, 2, 4), "FPS:");
        assert!(parser.screen().cell(2, 0).unwrap().inverse());
        assert!(!parser.screen().cell(1, 0).unwrap().inverse());
    }

    #[test]
    fn oversized_frames_are_clipped_without_scrolling() {
        let (width, height, pixels) = shaped_frame(10, 6);
        let frame = Frame {
            width,
            height,
            pixels: &pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (4, 3));

        // Had anything wrapped or scrolled, the corners would hold other cells
        for (col, row) in [(0, 0), (3, 2)] {
            let expected: Vec<Vt> = source_cell(&pixels, width, col, row)
                .into_iter()
                .map(rgb)
                .collect();
            assert_eq!(shown_cell(&parser, col, row), expected, "({col}, {row})");
        }
        assert_eq!(parser.screen().cursor_position().0, 2);
    }

    #[test]
    fn partial_cells_show_the_pixels_that_exist() {
        // Six pixel rows: one full cell row and half of another
        let (width, _, pixels) = shaped_frame(2, 2);
        let pixels = &pixels[..width as usize * 6 * 4];
        let frame = Frame {
            width,
            height: 6,
            pixels,
        };
        let parser = screen(OutputMode::TruecolorCharacters, frame, None, (2, 2));

        for col in 0..2 {
            let present_pixels = CELL_COLUMNS * 2;
            let expected: Vec<Vt> = source_cell(&pixels.repeat(2), width, col, 1)[..present_pixels]
                .iter()
                .map(|&color| rgb(color))
                .collect();
            assert_eq!(
                shown_cell(&parser, col, 1)[..present_pixels],
                expected,
                "col {col}"
            );
        }
    }

    #[test]
    fn image_protocols_leave_no_text_behind() {
        // A frame larger than the screen, so cropping is exercised too
        let pixels = vec![120; 50 * 70 * 4];
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

    fn compose_kitty(write_shared: &mut WriteShared<'_>) -> String {
        let pixels = vec![120; 20 * 40 * 4];
        let frame = Frame {
            width: 20,
            height: 40,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        compose_frame(
            OutputMode::Kitty,
            PIXEL_CELL_SIZE,
            frame,
            None,
            (4, 3),
            KittyImage {
                id: 1,
                write_shared: Some(write_shared),
                placeholders_drawn: None,
            },
            &mut out,
        )
        .unwrap();
        String::from_utf8(out).unwrap()
    }

    #[test]
    fn kitty_sends_shared_memory_names() {
        let mut written = Vec::new();
        let text = compose_kitty(&mut |data: &[u8]| {
            written.push(data.len());
            Ok("/dcat-test".into())
        });
        // The frame went to shared memory and only its name to the terminal
        assert_eq!(written, [20 * 40 * 4]);
        assert!(text.contains("t=s,"), "{text}");
        assert!(!text.contains("o=z"), "{text}");
    }

    #[test]
    fn kitty_in_tmux_uses_placeholders() {
        let pixels = vec![120; 20 * 40 * 4];
        let frame = Frame {
            width: 20,
            height: 40,
            pixels: &pixels,
        };
        let mut drawn = None;
        let compose = |drawn: &mut Option<(u16, u16)>| {
            let mut out = Vec::new();
            let kitty = KittyImage {
                id: 1,
                write_shared: None,
                placeholders_drawn: Some(drawn),
            };
            compose_frame(
                OutputMode::Kitty,
                PIXEL_CELL_SIZE,
                frame,
                None,
                (4, 3),
                kitty,
                &mut out,
            )
            .unwrap();
            String::from_utf8(out).unwrap()
        };

        let first = compose(&mut drawn);
        // The image goes through tmux into a 2x2 cell virtual placement
        assert!(
            !first.replace("\x1b\x1b", "").contains("\x1b_G"),
            "{first:?}"
        );
        assert!(first.contains("\x1bPtmux;\x1b\x1b_Ga=T,"), "{first:?}");
        assert!(first.contains(",U=1,c=2,r=2,"), "{first:?}");
        assert_eq!(first.matches('\u{10EEEE}').count(), 4);
        assert_eq!(drawn, Some((2, 2)));

        assert!(!first.contains("\x1b[?2026h"), "{first:?}");

        // tmux keeps the placeholders, so they aren't sent again
        let second = compose(&mut drawn);
        assert!(second.contains("\x1bPtmux;"));
        assert_eq!(second.matches('\u{10EEEE}').count(), 0);
    }

    #[test]
    fn kitty_falls_back_when_shared_memory_fails() {
        let text = compose_kitty(&mut |_: &[u8]| Err(io::Error::other("no space")));
        assert!(text.contains("o=z"), "{text}");
        assert!(!text.contains("t=s"), "{text}");
    }

    #[test]
    fn sixel_keeps_a_spare_row() {
        assert_eq!(reserved_rows(OutputMode::Sixel, false), 1);
        assert_eq!(reserved_rows(OutputMode::Kitty, false), 0);
        assert_eq!(reserved_rows(OutputMode::TruecolorCharacters, true), 1);
    }
}
