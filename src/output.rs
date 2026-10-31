use std::{
    io::{self, Write},
    process::{Command, Stdio},
};

use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use crossterm::{
    cursor::MoveTo,
    queue,
    style::{Color, Print, ResetColor, SetBackgroundColor, SetForegroundColor},
};
use flate2::{Compression, write::ZlibEncoder};

use crate::cli::OutputMode;

// A rendered frame of tightly packed RGBA pixels
#[derive(Clone, Copy)]
pub struct Frame<'a> {
    pub width: u32,
    pub height: u32,
    pub pixels: &'a [u8],
}

impl Frame<'_> {
    // Color of one pixel, ignoring alpha
    fn pixel(&self, x: usize, y: usize) -> [u8; 3] {
        let start = (y * self.width as usize + x) * 4;
        [
            self.pixels[start],
            self.pixels[start + 1],
            self.pixels[start + 2],
        ]
    }

    // The top-left part of the frame, at most width by height
    pub fn cropped(&self, width: u32, height: u32) -> Vec<u8> {
        let width = width.min(self.width) as usize;
        let height = height.min(self.height) as usize;
        let row_bytes = self.width as usize * 4;
        self.pixels
            .chunks_exact(row_bytes)
            .take(height)
            .flat_map(|row| &row[..width * 4])
            .copied()
            .collect()
    }
}

impl OutputMode {
    // Whether the mode draws with text characters rather than images
    pub fn uses_characters(self) -> bool {
        !matches!(self, Self::Kitty | Self::Sixel)
    }

    // One word for the mode, as --detect-output prints it
    pub fn name(self) -> &'static str {
        match self {
            Self::Sixel => "sixel",
            Self::Kitty => "kitty",
            Self::TruecolorCharacters => "truecolor",
            Self::PaletteCharacters => "palette",
            Self::BlockCharacters => "block",
        }
    }
}

// The output mode forced on the command line, or else the detected one
pub fn choose(forced: Option<OutputMode>) -> OutputMode {
    let env = |name: &str| std::env::var(name).ok();
    forced.unwrap_or_else(|| detect(env, || TmuxClient::query(env("TMUX_PANE"))))
}

// What tmux reports about the terminal it runs in
#[derive(Debug, Default, PartialEq)]
pub struct TmuxClient {
    // Its TERM and its name for itself, like "xterm-kitty kitty(0.49.1)"
    terminal: String,
    // Like "256,RGB,sixel"
    features: String,
    // Whether tmux was built with Sixel support
    sixel_support: bool,
    // Whether programs may pass escape sequences through to the terminal
    passthrough: bool,
}

const TMUX_FORMAT: &str = "#{client_termname} #{client_termtype}\t#{client_termfeatures}\t\
                           #{sixel_support}\t#{allow-passthrough}";

impl TmuxClient {
    // Ask tmux about the terminal showing `pane`
    pub fn query(pane: Option<String>) -> Option<Self> {
        let mut command = Command::new("tmux");
        command.args(["display-message", "-p"]);
        if let Some(pane) = pane {
            command.args(["-t", &pane]);
        }
        let output = command
            .arg(TMUX_FORMAT)
            .stdin(Stdio::null())
            .stderr(Stdio::null())
            .output()
            .ok()?;
        output
            .status
            .success()
            .then(|| Self::parse(&String::from_utf8_lossy(&output.stdout)))?
    }

    fn parse(reply: &str) -> Option<Self> {
        let mut fields = reply.trim_end_matches('\n').split('\t');
        let mut next = || fields.next();
        let (terminal, features, sixel_support, passthrough) = (next()?, next()?, next()?, next()?);
        Some(Self {
            terminal: terminal.to_lowercase(),
            features: features.into(),
            // Older versions leave out what they don't know about
            sixel_support: sixel_support == "1",
            passthrough: matches!(passthrough, "on" | "all"),
        })
    }

    fn has_feature(&self, name: &str) -> bool {
        self.features.split(',').any(|feature| feature == name)
    }
}

// Pick the best output mode the terminal is known to support, judging by its
// environment variables, or inside tmux by what tmux reports
pub fn detect(
    env: impl Fn(&str) -> Option<String>,
    tmux: impl FnOnce() -> Option<TmuxClient>,
) -> OutputMode {
    // tmux keeps graphics from reaching the terminal unless asked to. GNU
    // screen and Neovim's terminal drop them.
    if env("TMUX").is_some() {
        return tmux().map_or_else(
            || characters(&env, false),
            |client| inside_tmux(&env, &client),
        );
    }
    if env("STY").is_some() || env("NVIM").is_some() {
        return characters(&env, false);
    }

    let term = env("TERM").unwrap_or_default();
    let term_program = env("TERM_PROGRAM").unwrap_or_default();

    let kitty_graphics = env("KITTY_WINDOW_ID").is_some()
        || matches!(term.as_str(), "xterm-kitty" | "xterm-ghostty")
        || matches!(term_program.as_str(), "ghostty" | "WezTerm");
    if kitty_graphics {
        return OutputMode::Kitty;
    }

    let sixel = env("KONSOLE_VERSION").is_some()
        || ["foot", "mlterm", "contour", "yaft"]
            .iter()
            .any(|name| term.starts_with(name))
        || term_program == "iTerm.app";
    if sixel {
        return OutputMode::Sixel;
    }
    characters(&env, false)
}

fn inside_tmux(env: impl Fn(&str) -> Option<String>, client: &TmuxClient) -> OutputMode {
    // Kitty images pass through tmux, shown by placeholder text that tmux
    // moves around like any other. WezTerm doesn't support the placeholders.
    if client.passthrough
        && ["kitty", "ghostty"]
            .iter()
            .any(|name| client.terminal.contains(name))
    {
        return OutputMode::Kitty;
    }
    // tmux draws Sixel images itself
    if client.sixel_support && client.has_feature("sixel") {
        return OutputMode::Sixel;
    }
    characters(env, client.has_feature("RGB"))
}

fn characters(env: impl Fn(&str) -> Option<String>, truecolor: bool) -> OutputMode {
    let term = env("TERM").unwrap_or_default();
    let colorterm = env("COLORTERM").unwrap_or_default();
    if truecolor
        || matches!(colorterm.as_str(), "truecolor" | "24bit")
        || env("WT_SESSION").is_some()
        || cfg!(windows)
    {
        return OutputMode::TruecolorCharacters;
    }
    if matches!(term.as_str(), "linux" | "vt100" | "vt220" | "dumb") {
        return OutputMode::BlockCharacters;
    }
    OutputMode::PaletteCharacters
}

// Source pixels covered by one character cell in the character modes. Each
// pixel is half a cell wide and a quarter of it tall, which is roughly square.
pub const CELL_COLUMNS: usize = 2;
pub const CELL_ROWS: usize = 4;
const CELL_PIXELS: usize = CELL_COLUMNS * CELL_ROWS;

// Masks over a cell's pixels: bit (row * 2 + column) is set where the
// character draws in the foreground color
const UPPER_LEFT: u8 = 0b0000_0101;
const UPPER_RIGHT: u8 = 0b0000_1010;
const LOWER_LEFT: u8 = 0b0101_0000;
const LOWER_RIGHT: u8 = 0b1010_0000;
const BOTTOM_ROW: u8 = 0b1100_0000;
const BOTTOM_THREE_ROWS: u8 = 0b1111_1100;

// Block characters that line up with the 2x4 grid. Each also covers its
// inverse by swapping colors, so a full block isn't needed.
const SYMBOLS: [(char, u8); 17] = [
    (' ', 0),
    ('▘', UPPER_LEFT),
    ('▝', UPPER_RIGHT),
    ('▖', LOWER_LEFT),
    ('▗', LOWER_RIGHT),
    ('▀', UPPER_LEFT | UPPER_RIGHT),
    ('▄', LOWER_LEFT | LOWER_RIGHT),
    ('▌', UPPER_LEFT | LOWER_LEFT),
    ('▐', UPPER_RIGHT | LOWER_RIGHT),
    ('▚', UPPER_LEFT | LOWER_RIGHT),
    ('▞', UPPER_RIGHT | LOWER_LEFT),
    ('▛', UPPER_LEFT | UPPER_RIGHT | LOWER_LEFT),
    ('▜', UPPER_LEFT | UPPER_RIGHT | LOWER_RIGHT),
    ('▙', UPPER_LEFT | LOWER_LEFT | LOWER_RIGHT),
    ('▟', UPPER_RIGHT | LOWER_LEFT | LOWER_RIGHT),
    ('▂', BOTTOM_ROW),
    ('▆', BOTTOM_THREE_ROWS),
];

// A cell must improve on a flat color by this much squared error before
// it's drawn with a shape, so near-flat areas stay cheap spaces
const MIN_SHAPE_IMPROVEMENT: f32 = 64.0;

// The pixels a character covers in the foreground color
pub fn symbol_mask(symbol: char) -> Option<u8> {
    SYMBOLS
        .iter()
        .find(|&&(c, _)| c == symbol)
        .map(|&(_, mask)| mask)
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CellSymbol {
    pub symbol: char,
    pub foreground: [u8; 3],
    pub background: [u8; 3],
}

// Pick the character and two colors that best reproduce a cell. Only pixels
// in `present` take part, for cells at the frame's edge.
pub fn best_symbol(pixels: &[[u8; 3]; CELL_PIXELS], present: u8) -> CellSymbol {
    // Per channel color sums and pixel count over a mask
    let sums = |mask: u8| {
        let mut sum = [0f32; 3];
        let mut count = 0f32;
        for (bit, pixel) in pixels.iter().enumerate() {
            if mask & present & (1 << bit) != 0 {
                for (total, &value) in sum.iter_mut().zip(pixel) {
                    *total += f32::from(value);
                }
                count += 1.0;
            }
        }
        (sum, count)
    };
    let mean = |(sum, count): ([f32; 3], f32)| sum.map(|s| (s / count).round() as u8);
    // Squared error is constant minus sum^2 / count for each region, so the
    // best split maximizes this
    let score = |(sum, count): ([f32; 3], f32)| sum.iter().map(|s| s * s / count).sum::<f32>();

    let all = sums(u8::MAX);
    let flat = CellSymbol {
        symbol: ' ',
        foreground: mean(all),
        background: mean(all),
    };
    let mut best = (score(all) + MIN_SHAPE_IMPROVEMENT, flat);

    for &(symbol, mask) in &SYMBOLS[1..] {
        let (inside, outside) = (sums(mask), sums(!mask));
        if inside.1 == 0.0 || outside.1 == 0.0 {
            continue;
        }
        let candidate = score(inside) + score(outside);
        if candidate > best.0 {
            best = (
                candidate,
                CellSymbol {
                    symbol,
                    foreground: mean(inside),
                    background: mean(outside),
                },
            );
        }
    }
    best.1
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ColorDepth {
    Truecolor,
    // The 240 colors of the xterm palette that don't depend on the theme
    Palette,
}

// The cell's pixels and a mask of those inside the frame
fn cell_pixels(frame: Frame, col: usize, row: usize) -> ([[u8; 3]; CELL_PIXELS], u8) {
    let mut pixels = [[0; 3]; CELL_PIXELS];
    let mut present = 0;
    for dy in 0..CELL_ROWS {
        for dx in 0..CELL_COLUMNS {
            let (x, y) = (col * CELL_COLUMNS + dx, row * CELL_ROWS + dy);
            if x < frame.width as usize && y < frame.height as usize {
                let bit = dy * CELL_COLUMNS + dx;
                pixels[bit] = frame.pixel(x, y);
                present |= 1 << bit;
            }
        }
    }
    (pixels, present)
}

// Cells the frame covers, limited to the space available
fn cell_extent(frame: Frame, cols: usize, rows: usize) -> (usize, usize) {
    (
        cols.min((frame.width as usize).div_ceil(CELL_COLUMNS)),
        rows.min((frame.height as usize).div_ceil(CELL_ROWS)),
    )
}

// Block characters with a foreground and background color per cell, each
// cell showing 2x4 source pixels
pub fn encode_symbols(
    frame: Frame,
    cols: usize,
    rows: usize,
    depth: ColorDepth,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let to_color = |[r, g, b]: [u8; 3]| match depth {
        ColorDepth::Truecolor => Color::Rgb { r, g, b },
        ColorDepth::Palette => Color::AnsiValue(ansi_colours::ansi256_from_rgb([r, g, b])),
    };
    let (cols, rows) = cell_extent(frame, cols, rows);

    // Colors persist between cells, so only emit them when they change
    let (mut foreground, mut background) = (None, None);
    for row in 0..rows {
        queue!(out, MoveTo(0, row as u16))?;
        for col in 0..cols {
            let (pixels, present) = cell_pixels(frame, col, row);
            let cell = best_symbol(&pixels, present);

            let new_background = to_color(cell.background);
            if background != Some(new_background) {
                queue!(out, SetBackgroundColor(new_background))?;
                background = Some(new_background);
            }
            // A space shows only the background
            if cell.symbol != ' ' {
                let new_foreground = to_color(cell.foreground);
                if foreground != Some(new_foreground) {
                    queue!(out, SetForegroundColor(new_foreground))?;
                    foreground = Some(new_foreground);
                }
            }
            queue!(out, Print(cell.symbol))?;
        }
    }
    queue!(out, ResetColor)
}

// 4x4 ordered dither thresholds, in sixteenths
const BAYER_4X4: [[u8; 4]; 4] = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];

// Characters for monochrome output with the fraction of their top and
// bottom halves drawn in the foreground color. All of them are in code page
// 437, so the Linux console's font has them. The shades give gray levels
// without a dithering pattern across cells.
const MONO_GLYPHS: [(char, f32, f32); 7] = [
    (' ', 0.0, 0.0),
    ('░', 0.25, 0.25),
    ('▒', 0.5, 0.5),
    ('▓', 0.75, 0.75),
    ('█', 1.0, 1.0),
    ('▀', 1.0, 0.0),
    ('▄', 0.0, 1.0),
];

// The monochrome character whose coverage best matches a cell's top and
// bottom brightness, each from 0 to 1
pub fn best_mono_glyph(top: f32, bottom: f32) -> char {
    let error = |&(_, glyph_top, glyph_bottom): &(char, f32, f32)| {
        (glyph_top - top).powi(2) + (glyph_bottom - bottom).powi(2)
    };
    MONO_GLYPHS
        .iter()
        .min_by(|a, b| error(a).total_cmp(&error(b)))
        .map_or(' ', |&(glyph, _, _)| glyph)
}

// Monochrome characters in the terminal's own foreground and background colors
pub fn encode_mono_blocks(
    frame: Frame,
    cols: usize,
    rows: usize,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let (cols, rows) = cell_extent(frame, cols, rows);
    // Average luminance of the top or bottom half of a cell, from 0 to 1
    let brightness = |pixels: &[[u8; 3]; CELL_PIXELS], present: u8, half: usize| {
        let half_rows = CELL_ROWS / 2;
        let (mut total, mut count) = (0.0, 0.0);
        for dy in half * half_rows..(half + 1) * half_rows {
            for dx in 0..CELL_COLUMNS {
                let bit = dy * CELL_COLUMNS + dx;
                if present & (1 << bit) != 0 {
                    let [r, g, b] = pixels[bit];
                    total += 0.2126 * f32::from(r) + 0.7152 * f32::from(g) + 0.0722 * f32::from(b);
                    count += 1.0;
                }
            }
        }
        if count > 0.0 {
            total / count / 255.0
        } else {
            0.0
        }
    };

    queue!(out, ResetColor)?;
    for row in 0..rows {
        queue!(out, MoveTo(0, row as u16))?;
        let line: String = (0..cols)
            .map(|col| {
                let (pixels, present) = cell_pixels(frame, col, row);
                best_mono_glyph(
                    brightness(&pixels, present, 0),
                    brightness(&pixels, present, 1),
                )
            })
            .collect();
        queue!(out, Print(line))?;
    }
    Ok(())
}

// Escape sequences are limited to this many bytes of base64 payload each
const KITTY_CHUNK_SIZE: usize = 4096;

// Kitty graphics protocol: compressed RGBA, replacing the previous frame's
// image and placement so they don't pile up. The cursor isn't moved, so a
// full-height image can't scroll the screen.
// Where a Kitty image is shown
#[derive(Clone, Copy)]
pub struct KittyPlacement {
    pub image_id: u32,
    // A virtual placement this many cells wide and tall, which Unicode
    // placeholders show (see encode_kitty_placeholders), or None to show the
    // image at the cursor
    pub virtual_cells: Option<(u16, u16)>,
}

impl KittyPlacement {
    fn keys(self) -> String {
        let id = self.image_id;
        match self.virtual_cells {
            Some((cols, rows)) => format!("i={id},p=1,q=2,U=1,c={cols},r={rows}"),
            None => format!("i={id},p=1,q=2,C=1"),
        }
    }
}

pub fn encode_kitty(frame: Frame, placement: KittyPlacement, out: &mut Vec<u8>) -> io::Result<()> {
    let mut encoder = ZlibEncoder::new(Vec::new(), Compression::fast());
    encoder.write_all(frame.pixels)?;
    let payload = BASE64.encode(encoder.finish()?);

    let chunks: Vec<&[u8]> = payload.as_bytes().chunks(KITTY_CHUNK_SIZE).collect();
    for (index, chunk) in chunks.iter().enumerate() {
        let more = u8::from(index + 1 < chunks.len());
        if index == 0 {
            write!(
                out,
                "\x1b_Ga=T,f=32,o=z,s={},v={},{},m={more};",
                frame.width,
                frame.height,
                placement.keys()
            )?;
        } else {
            write!(out, "\x1b_Gm={more};")?;
        }
        out.extend_from_slice(chunk);
        out.extend_from_slice(b"\x1b\\");
    }
    Ok(())
}

// Kitty graphics protocol with the pixels in a shared memory object, which
// the terminal reads and removes, instead of in the escape sequence
pub fn encode_kitty_shared(
    frame: Frame,
    placement: KittyPlacement,
    shm_name: &str,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    write!(
        out,
        "\x1b_Ga=T,f=32,t=s,s={},v={},S={},{};{}\x1b\\",
        frame.width,
        frame.height,
        frame.pixels.len(),
        placement.keys(),
        BASE64.encode(shm_name)
    )
}

// Kitty shows a virtual placement wherever this character is, in the image's
// id as foreground color, with diacritics giving the cell's row and column
const PLACEHOLDER: char = '\u{10EEEE}';
// The diacritics numbering placeholder rows and columns, from Kitty's
// rowcolumn-diacritics.txt
#[rustfmt::skip]
const PLACEHOLDER_DIACRITICS: [char; 297] = [
    '\u{0305}', '\u{030d}', '\u{030e}', '\u{0310}', '\u{0312}', '\u{033d}', '\u{033e}', '\u{033f}',
    '\u{0346}', '\u{034a}', '\u{034b}', '\u{034c}', '\u{0350}', '\u{0351}', '\u{0352}', '\u{0357}',
    '\u{035b}', '\u{0363}', '\u{0364}', '\u{0365}', '\u{0366}', '\u{0367}', '\u{0368}', '\u{0369}',
    '\u{036a}', '\u{036b}', '\u{036c}', '\u{036d}', '\u{036e}', '\u{036f}', '\u{0483}', '\u{0484}',
    '\u{0485}', '\u{0486}', '\u{0487}', '\u{0592}', '\u{0593}', '\u{0594}', '\u{0595}', '\u{0597}',
    '\u{0598}', '\u{0599}', '\u{059c}', '\u{059d}', '\u{059e}', '\u{059f}', '\u{05a0}', '\u{05a1}',
    '\u{05a8}', '\u{05a9}', '\u{05ab}', '\u{05ac}', '\u{05af}', '\u{05c4}', '\u{0610}', '\u{0611}',
    '\u{0612}', '\u{0613}', '\u{0614}', '\u{0615}', '\u{0616}', '\u{0617}', '\u{0657}', '\u{0658}',
    '\u{0659}', '\u{065a}', '\u{065b}', '\u{065d}', '\u{065e}', '\u{06d6}', '\u{06d7}', '\u{06d8}',
    '\u{06d9}', '\u{06da}', '\u{06db}', '\u{06dc}', '\u{06df}', '\u{06e0}', '\u{06e1}', '\u{06e2}',
    '\u{06e4}', '\u{06e7}', '\u{06e8}', '\u{06eb}', '\u{06ec}', '\u{0730}', '\u{0732}', '\u{0733}',
    '\u{0735}', '\u{0736}', '\u{073a}', '\u{073d}', '\u{073f}', '\u{0740}', '\u{0741}', '\u{0743}',
    '\u{0745}', '\u{0747}', '\u{0749}', '\u{074a}', '\u{07eb}', '\u{07ec}', '\u{07ed}', '\u{07ee}',
    '\u{07ef}', '\u{07f0}', '\u{07f1}', '\u{07f3}', '\u{0816}', '\u{0817}', '\u{0818}', '\u{0819}',
    '\u{081b}', '\u{081c}', '\u{081d}', '\u{081e}', '\u{081f}', '\u{0820}', '\u{0821}', '\u{0822}',
    '\u{0823}', '\u{0825}', '\u{0826}', '\u{0827}', '\u{0829}', '\u{082a}', '\u{082b}', '\u{082c}',
    '\u{082d}', '\u{0951}', '\u{0953}', '\u{0954}', '\u{0f82}', '\u{0f83}', '\u{0f86}', '\u{0f87}',
    '\u{135d}', '\u{135e}', '\u{135f}', '\u{17dd}', '\u{193a}', '\u{1a17}', '\u{1a75}', '\u{1a76}',
    '\u{1a77}', '\u{1a78}', '\u{1a79}', '\u{1a7a}', '\u{1a7b}', '\u{1a7c}', '\u{1b6b}', '\u{1b6d}',
    '\u{1b6e}', '\u{1b6f}', '\u{1b70}', '\u{1b71}', '\u{1b72}', '\u{1b73}', '\u{1cd0}', '\u{1cd1}',
    '\u{1cd2}', '\u{1cda}', '\u{1cdb}', '\u{1ce0}', '\u{1dc0}', '\u{1dc1}', '\u{1dc3}', '\u{1dc4}',
    '\u{1dc5}', '\u{1dc6}', '\u{1dc7}', '\u{1dc8}', '\u{1dc9}', '\u{1dcb}', '\u{1dcc}', '\u{1dd1}',
    '\u{1dd2}', '\u{1dd3}', '\u{1dd4}', '\u{1dd5}', '\u{1dd6}', '\u{1dd7}', '\u{1dd8}', '\u{1dd9}',
    '\u{1dda}', '\u{1ddb}', '\u{1ddc}', '\u{1ddd}', '\u{1dde}', '\u{1ddf}', '\u{1de0}', '\u{1de1}',
    '\u{1de2}', '\u{1de3}', '\u{1de4}', '\u{1de5}', '\u{1de6}', '\u{1dfe}', '\u{20d0}', '\u{20d1}',
    '\u{20d4}', '\u{20d5}', '\u{20d6}', '\u{20d7}', '\u{20db}', '\u{20dc}', '\u{20e1}', '\u{20e7}',
    '\u{20e9}', '\u{20f0}', '\u{2cef}', '\u{2cf0}', '\u{2cf1}', '\u{2de0}', '\u{2de1}', '\u{2de2}',
    '\u{2de3}', '\u{2de4}', '\u{2de5}', '\u{2de6}', '\u{2de7}', '\u{2de8}', '\u{2de9}', '\u{2dea}',
    '\u{2deb}', '\u{2dec}', '\u{2ded}', '\u{2dee}', '\u{2def}', '\u{2df0}', '\u{2df1}', '\u{2df2}',
    '\u{2df3}', '\u{2df4}', '\u{2df5}', '\u{2df6}', '\u{2df7}', '\u{2df8}', '\u{2df9}', '\u{2dfa}',
    '\u{2dfb}', '\u{2dfc}', '\u{2dfd}', '\u{2dfe}', '\u{2dff}', '\u{a66f}', '\u{a67c}', '\u{a67d}',
    '\u{a6f0}', '\u{a6f1}', '\u{a8e0}', '\u{a8e1}', '\u{a8e2}', '\u{a8e3}', '\u{a8e4}', '\u{a8e5}',
    '\u{a8e6}', '\u{a8e7}', '\u{a8e8}', '\u{a8e9}', '\u{a8ea}', '\u{a8eb}', '\u{a8ec}', '\u{a8ed}',
    '\u{a8ee}', '\u{a8ef}', '\u{a8f0}', '\u{a8f1}', '\u{aab0}', '\u{aab2}', '\u{aab3}', '\u{aab7}',
    '\u{aab8}', '\u{aabe}', '\u{aabf}', '\u{aac1}', '\u{fe20}', '\u{fe21}', '\u{fe22}', '\u{fe23}',
    '\u{fe24}', '\u{fe25}', '\u{fe26}', '\u{10a0f}', '\u{10a38}', '\u{1d185}', '\u{1d186}',
    '\u{1d187}', '\u{1d188}', '\u{1d189}', '\u{1d1aa}', '\u{1d1ab}', '\u{1d1ac}', '\u{1d1ad}',
    '\u{1d242}', '\u{1d243}', '\u{1d244}',
];

// Placeholder text for a virtual placement of `cols` by `rows` cells, starting
// at the top left. Only the first cell of each row is numbered, and the rest
// count on from it.
pub fn encode_kitty_placeholders(
    image_id: u32,
    (cols, rows): (u16, u16),
    out: &mut Vec<u8>,
) -> io::Result<()> {
    // Ids that fit go in a 256-color index, which tmux passes on as it is.
    // Bigger ones need truecolor, with a third diacritic for the top byte.
    let (color, top_byte) = match u8::try_from(image_id) {
        Ok(index) => (Color::AnsiValue(index), None),
        Err(_) => {
            let [top, r, g, b] = image_id.to_be_bytes();
            (Color::Rgb { r, g, b }, Some(top))
        }
    };
    queue!(out, SetForegroundColor(color))?;
    for row in 0..rows.min(PLACEHOLDER_DIACRITICS.len() as u16) {
        let mut text = String::from(PLACEHOLDER);
        text.push(PLACEHOLDER_DIACRITICS[usize::from(row)]);
        text.push(PLACEHOLDER_DIACRITICS[0]);
        if let Some(top) = top_byte {
            text.push(PLACEHOLDER_DIACRITICS[usize::from(top)]);
        }
        text.extend(std::iter::repeat_n(
            PLACEHOLDER,
            usize::from(cols).saturating_sub(1),
        ));
        queue!(out, MoveTo(0, row), Print(text))?;
    }
    queue!(out, ResetColor)
}

// Wrap escape sequences, each ending in ST, so tmux passes them on to the
// terminal: in a tmux DCS with every ESC doubled. Each goes in its own, since
// tmux drops long ones.
pub fn wrap_for_tmux(sequences: &[u8], out: &mut Vec<u8>) {
    let mut rest = sequences;
    while !rest.is_empty() {
        let end = rest
            .windows(2)
            .position(|window| window == b"\x1b\\")
            .map_or(rest.len(), |start| start + 2);
        let (sequence, tail) = rest.split_at(end);
        out.extend_from_slice(b"\x1bPtmux;");
        for &byte in sequence {
            if byte == 0x1b {
                out.push(0x1b);
            }
            out.push(byte);
        }
        out.extend_from_slice(b"\x1b\\");
        rest = tail;
    }
}

// Removes dcat's image on exit, leaving other programs' images alone
pub fn kitty_delete(image_id: u32) -> String {
    format!("\x1b_Ga=d,d=I,i={image_id},q=2\x1b\\")
}

// Sixel palette: 6 levels of red and blue and 7 of green, which the eye is most sensitive to
const SIXEL_LEVELS: [usize; 3] = [6, 7, 6];
const SIXEL_PALETTE_SIZE: usize = SIXEL_LEVELS[0] * SIXEL_LEVELS[1] * SIXEL_LEVELS[2];

fn sixel_palette_color(index: usize) -> [u8; 3] {
    let [r_levels, g_levels, b_levels] = SIXEL_LEVELS;
    let level = |value: usize, levels: usize| (value * 255 / (levels - 1)) as u8;
    [
        level(index / (g_levels * b_levels), r_levels),
        level(index / b_levels % g_levels, g_levels),
        level(index % b_levels, b_levels),
    ]
}

// Palette level for each dither threshold and channel value, so quantizing a
// pixel is three table lookups
fn sixel_level_table(levels: usize) -> [[u8; 256]; 16] {
    let mut table = [[0u8; 256]; 16];
    for (threshold_index, row) in table.iter_mut().enumerate() {
        let threshold = (threshold_index as f32 + 0.5) / 16.0;
        for (value, level) in row.iter_mut().enumerate() {
            let scaled = value as f32 / 255.0 * (levels - 1) as f32;
            *level = ((scaled + threshold - 0.5).round() as usize).min(levels - 1) as u8;
        }
    }
    table
}

// Map each pixel to a palette entry, dithering so gradients don't band
fn sixel_quantize(frame: Frame) -> Vec<u8> {
    let [r_levels, g_levels, b_levels] = SIXEL_LEVELS;
    let (r_table, g_table, b_table) = (
        sixel_level_table(r_levels),
        sixel_level_table(g_levels),
        sixel_level_table(b_levels),
    );

    let width = frame.width as usize;
    let mut indices = Vec::with_capacity(width * frame.height as usize);
    for (y, row) in frame.pixels.chunks_exact(width * 4).enumerate() {
        let thresholds = &BAYER_4X4[y % 4];
        for (x, [r, g, b, _]) in row.as_chunks::<4>().0.iter().enumerate() {
            let t = thresholds[x % 4] as usize;
            let index = usize::from(r_table[t][*r as usize]) * g_levels * b_levels
                + usize::from(g_table[t][*g as usize]) * b_levels
                + usize::from(b_table[t][*b as usize]);
            indices.push(index as u8);
        }
    }
    indices
}

pub fn encode_sixel(frame: Frame, out: &mut Vec<u8>) -> io::Result<()> {
    let width = frame.width as usize;
    let height = frame.height as usize;
    let indices = sixel_quantize(frame);

    // Start sixel data with 1:1 pixel aspect ratio and the image size
    write!(out, "\x1bPq\"1;1;{width};{height}")?;

    let mut used = [false; SIXEL_PALETTE_SIZE];
    for &index in &indices {
        used[index as usize] = true;
    }
    for (index, _) in used.iter().enumerate().filter(|&(_, &used)| used) {
        let [r, g, b] = sixel_palette_color(index).map(|c| u32::from(c) * 100 / 255);
        write!(out, "#{index};2;{r};{g};{b}")?;
    }

    // Each band is six pixel rows; each color in it is drawn as one line of
    // sixels, overprinting the same band
    let mut band_bits = vec![0u8; SIXEL_PALETTE_SIZE * width];
    // Columns each color spans within the current band
    let mut spans = vec![(usize::MAX, 0usize); SIXEL_PALETTE_SIZE];
    let mut band_colors = Vec::with_capacity(SIXEL_PALETTE_SIZE);
    for band_top in (0..height).step_by(6) {
        for row in 0..6.min(height - band_top) {
            let y = band_top + row;
            for (x, &color) in indices[y * width..(y + 1) * width].iter().enumerate() {
                let color = color as usize;
                band_bits[color * width + x] |= 1 << row;
                let span = &mut spans[color];
                if span.0 == usize::MAX {
                    band_colors.push(color);
                    span.0 = x;
                }
                span.0 = span.0.min(x);
                span.1 = span.1.max(x);
            }
        }

        band_colors.sort_unstable();
        for (i, &color) in band_colors.iter().enumerate() {
            if i > 0 {
                // Back to the start of the band for the next color
                out.push(b'$');
            }
            write!(out, "#{color}")?;
            // Nothing needs drawing after the color's last column
            let (start, end) = spans[color];
            let row = &mut band_bits[color * width..(color + 1) * width];
            write_sixel_runs(&row[..=end], out)?;

            // Reset only what this band used
            row[start..=end].fill(0);
            spans[color] = (usize::MAX, 0);
        }
        band_colors.clear();
        out.push(b'-');
    }

    out.extend_from_slice(b"\x1b\\");
    Ok(())
}

// Write one line of sixels, run-length encoding repeats
fn write_sixel_runs(bits: &[u8], out: &mut Vec<u8>) -> io::Result<()> {
    let mut x = 0;
    while x < bits.len() {
        let value = bits[x];
        let run = bits[x..].iter().take_while(|&&b| b == value).count();
        let character = b'?' + value;
        if run > 3 {
            write!(out, "!{run}")?;
            out.push(character);
        } else {
            out.extend(std::iter::repeat_n(character, run));
        }
        x += run;
    }
    Ok(())
}

// Stops sixel images from scrolling the screen and places them at the top left
pub const SIXEL_DISABLE_SCROLLING: &str = "\x1b[?80h";
pub const SIXEL_ENABLE_SCROLLING: &str = "\x1b[?80l";

#[cfg(test)]
mod tests {
    use super::*;
    use std::{collections::HashMap, io::Read};

    fn env<'a>(vars: &'a [(&'a str, &'a str)]) -> impl Fn(&str) -> Option<String> + 'a {
        let map: HashMap<_, _> = vars.iter().copied().collect();
        move |name| map.get(name).map(|value| value.to_string())
    }

    fn tmux_client(reply: &str) -> TmuxClient {
        TmuxClient::parse(reply).unwrap()
    }

    #[test]
    fn tmux_replies() {
        assert_eq!(
            tmux_client("xterm-kitty kitty(0.49.1)\t256,RGB,sixel\t1\ton\n"),
            TmuxClient {
                terminal: "xterm-kitty kitty(0.49.1)".into(),
                features: "256,RGB,sixel".into(),
                sixel_support: true,
                passthrough: true,
            }
        );
        // tmux before 3.3 knows neither Sixel nor passthrough
        let old = tmux_client("xterm-256color \t256\t\t\n");
        assert!(!old.sixel_support && !old.passthrough);
        assert_eq!(TmuxClient::parse("not a reply"), None);
    }

    #[test]
    fn detection_inside_multiplexers() {
        let in_tmux = env(&[
            ("TMUX", "/tmp/tmux-1000/default,1,0"),
            ("TERM", "tmux-256color"),
        ]);
        let cases = [
            (
                "xterm-kitty kitty(0.49.1)\t256,RGB\t1\ton",
                OutputMode::Kitty,
            ),
            (
                "xterm-ghostty ghostty 1.2\t256,RGB\t1\tall",
                OutputMode::Kitty,
            ),
            // Kitty graphics need passthrough
            (
                "xterm-kitty kitty(0.49.1)\t256,RGB\t1\toff",
                OutputMode::TruecolorCharacters,
            ),
            ("foot foot(1.20)\t256,RGB,sixel\t1\toff", OutputMode::Sixel),
            // Sixel needs tmux built with it
            (
                "foot foot(1.20)\t256,RGB,sixel\t0\toff",
                OutputMode::TruecolorCharacters,
            ),
            (
                "wezterm WezTerm 2024\t256,RGB,sixel\t1\ton",
                OutputMode::Sixel,
            ),
        ];
        for (reply, expected) in cases {
            assert_eq!(
                detect(&in_tmux, || Some(tmux_client(reply))),
                expected,
                "{reply:?}"
            );
        }

        // Without an answer from tmux, the terminal outside is unknown
        let kitty_in_tmux = env(&[
            ("TMUX", "/tmp/tmux-1000/default,1,0"),
            ("KITTY_WINDOW_ID", "1"),
            ("COLORTERM", "truecolor"),
        ]);
        assert_eq!(
            detect(kitty_in_tmux, || None),
            OutputMode::TruecolorCharacters
        );

        // GNU screen and Neovim's terminal never pass graphics on
        for (name, value) in [
            ("STY", "1234.pts-0.host"),
            ("NVIM", "/run/user/1000/nvim.1.0"),
        ] {
            let vars = [
                (name, value),
                ("TERM", "xterm-kitty"),
                ("COLORTERM", "truecolor"),
            ];
            assert_eq!(
                detect(env(&vars), || None),
                OutputMode::TruecolorCharacters,
                "{name}"
            );
        }
    }

    #[test]
    fn detection() {
        let cases: &[(&[(&str, &str)], OutputMode)] = &[
            (
                &[("KITTY_WINDOW_ID", "1"), ("TERM", "xterm-kitty")],
                OutputMode::Kitty,
            ),
            (&[("TERM_PROGRAM", "WezTerm")], OutputMode::Kitty),
            (&[("TERM", "xterm-ghostty")], OutputMode::Kitty),
            (&[("TERM", "foot")], OutputMode::Sixel),
            (
                &[("TERM", "xterm-256color"), ("KONSOLE_VERSION", "240401")],
                OutputMode::Sixel,
            ),
            (
                &[("TERM", "xterm-256color"), ("COLORTERM", "truecolor")],
                OutputMode::TruecolorCharacters,
            ),
        ];
        for (vars, expected) in cases {
            assert_eq!(detect(env(vars), || None), *expected, "{vars:?}");
        }
        // Windows terminals all support truecolor, so the rest only apply elsewhere
        if cfg!(windows) {
            assert_eq!(
                detect(env(&[("TERM", "linux")]), || None),
                OutputMode::TruecolorCharacters
            );
            assert_eq!(detect(env(&[]), || None), OutputMode::TruecolorCharacters);
        } else {
            assert_eq!(
                detect(env(&[("TERM", "linux")]), || None),
                OutputMode::BlockCharacters
            );
            assert_eq!(
                detect(env(&[("TERM", "xterm-256color")]), || None),
                OutputMode::PaletteCharacters
            );
            assert_eq!(detect(env(&[]), || None), OutputMode::PaletteCharacters);
        }
    }

    // Opaque RGBA pixels from RGB colors
    fn frame_from(width: u32, height: u32, pixels: &[[u8; 3]]) -> Vec<u8> {
        assert_eq!(pixels.len(), (width * height) as usize);
        pixels
            .iter()
            .flat_map(|&[r, g, b]| [r, g, b, 255])
            .collect()
    }

    // The pixels a chosen symbol shows on screen
    fn reconstruct(cell: CellSymbol) -> [[u8; 3]; CELL_PIXELS] {
        let mask = symbol_mask(cell.symbol).unwrap();
        std::array::from_fn(|bit| {
            if mask & (1 << bit) != 0 {
                cell.foreground
            } else {
                cell.background
            }
        })
    }

    // Each character's glyph drawn on the 2x4 grid, from its Unicode name,
    // independently of the SYMBOLS table
    #[test]
    fn symbol_masks_match_the_glyphs() {
        let glyphs = [
            (' ', ".. .. .. .."), // space
            ('▘', "#. #. .. .."), // quadrant upper left
            ('▝', ".# .# .. .."), // quadrant upper right
            ('▖', ".. .. #. #."), // quadrant lower left
            ('▗', ".. .. .# .#"), // quadrant lower right
            ('▀', "## ## .. .."), // upper half block
            ('▄', ".. .. ## ##"), // lower half block
            ('▌', "#. #. #. #."), // left half block
            ('▐', ".# .# .# .#"), // right half block
            ('▚', "#. #. .# .#"), // upper left and lower right
            ('▞', ".# .# #. #."), // upper right and lower left
            ('▛', "## ## #. #."), // upper left, upper right, lower left
            ('▜', "## ## .# .#"), // upper left, upper right, lower right
            ('▙', "#. #. ## ##"), // upper left, lower left, lower right
            ('▟', ".# .# ## ##"), // upper right, lower left, lower right
            ('▂', ".. .. .. ##"), // lower one quarter block
            ('▆', ".. ## ## ##"), // lower three quarters block
        ];
        assert_eq!(glyphs.len(), SYMBOLS.len());
        for (symbol, art) in glyphs {
            let mask = art
                .chars()
                .filter(|c| !c.is_whitespace())
                .enumerate()
                .fold(0u8, |mask, (bit, c)| mask | (u8::from(c == '#') << bit));
            assert_eq!(symbol_mask(symbol), Some(mask), "{symbol:?}");
        }
    }

    #[test]
    fn every_symbol_is_reproduced_exactly() {
        let (a, b) = ([200, 40, 10], [10, 60, 220]);
        for &(symbol, mask) in &SYMBOLS {
            // Each symbol and its inverse, which swaps the colors
            for mask in [mask, !mask] {
                let pixels = std::array::from_fn(|bit| if mask & (1 << bit) != 0 { a } else { b });
                let cell = best_symbol(&pixels, u8::MAX);
                assert_eq!(reconstruct(cell), pixels, "{symbol:?} {mask:08b}: {cell:?}");
            }
        }
    }

    #[test]
    fn flat_and_nearly_flat_cells_are_spaces() {
        let flat = [[90, 90, 90]; CELL_PIXELS];
        let cell = best_symbol(&flat, u8::MAX);
        assert_eq!((cell.symbol, cell.background), (' ', [90, 90, 90]));

        let mut noisy = flat;
        noisy[3] = [92, 90, 91];
        assert_eq!(best_symbol(&noisy, u8::MAX).symbol, ' ');
    }

    #[test]
    fn missing_pixels_are_ignored() {
        // Only the top two rows are inside the frame: red on the left, blue
        // on the right. The rest is black and must not pull the colors.
        let (red, blue) = ([255, 0, 0], [0, 0, 255]);
        let mut pixels = [[0; 3]; CELL_PIXELS];
        for row in 0..2 {
            pixels[row * 2] = red;
            pixels[row * 2 + 1] = blue;
        }
        let shown = reconstruct(best_symbol(&pixels, 0b0000_1111));
        assert_eq!(shown[..4], pixels[..4]);
    }

    fn encode(frame: Frame, cols: usize, rows: usize, depth: ColorDepth) -> String {
        let mut out = Vec::new();
        encode_symbols(frame, cols, rows, depth, &mut out).unwrap();
        String::from_utf8(out).unwrap()
    }

    #[test]
    fn colors_are_sent_only_when_they_change() {
        // Two flat gray cells, then one gray on the left and red on the right
        let (gray, red) = ([128; 3], [255, 0, 0]);
        let row = [gray, gray, gray, gray, gray, red];
        let pixels = frame_from(6, 4, &row.repeat(4));
        let frame = Frame {
            width: 6,
            height: 4,
            pixels: &pixels,
        };
        let text = encode(frame, 3, 1, ColorDepth::Truecolor);
        assert_eq!(text.matches(' ').count(), 2, "{text:?}");
        // The gray background is set once for both flat cells
        assert_eq!(text.matches("48;2;128;128;128").count(), 1, "{text:?}");
        assert_eq!(text.matches("2;255;0;0").count(), 1, "{text:?}");
    }

    #[test]
    fn palette_mode_uses_palette_colors() {
        let pixels = frame_from(2, 4, &[[128; 3]; 8]);
        let frame = Frame {
            width: 2,
            height: 4,
            pixels: &pixels,
        };
        assert!(encode(frame, 1, 1, ColorDepth::Palette).contains("48;5;244"));
    }

    #[test]
    fn symbols_are_clipped_to_the_available_cells() {
        let pixels = vec![200; 8 * 8 * 4];
        let frame = Frame {
            width: 8,
            height: 8,
            pixels: &pixels,
        };
        // The frame covers 4x2 cells but only 2x1 are available
        assert_eq!(
            encode(frame, 2, 1, ColorDepth::Truecolor)
                .matches(' ')
                .count(),
            2
        );
    }

    #[test]
    fn mono_blocks() {
        // Four cells: lit top half, lit bottom half, all lit, all dark
        let (white, black) = ([255; 3], [0; 3]);
        let top = [white, white, black, black, white, white, black, black];
        let bottom = [black, black, white, white, white, white, black, black];
        let pixels = frame_from(8, 4, &[top, top, bottom, bottom].concat());
        let frame = Frame {
            width: 8,
            height: 4,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        encode_mono_blocks(frame, 4, 1, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.ends_with("▀▄█ "), "{text:?}");
    }

    #[test]
    fn mono_shades_show_gray_levels() {
        assert_eq!(best_mono_glyph(0.0, 0.0), ' ');
        assert_eq!(best_mono_glyph(0.27, 0.22), '░');
        assert_eq!(best_mono_glyph(0.5, 0.5), '▒');
        assert_eq!(best_mono_glyph(0.7, 0.8), '▓');
        assert_eq!(best_mono_glyph(1.0, 0.95), '█');
        // A sharp edge between the halves still uses a half block
        assert_eq!(best_mono_glyph(0.9, 0.1), '▀');
        assert_eq!(best_mono_glyph(0.0, 1.0), '▄');
    }

    #[test]
    fn crop() {
        let pixels: Vec<u8> = (0..4 * 2 * 4).map(|v| v as u8).collect();
        let frame = Frame {
            width: 4,
            height: 2,
            pixels: &pixels,
        };
        assert_eq!(frame.cropped(2, 1), (0..8).collect::<Vec<u8>>());
        assert_eq!(frame.cropped(10, 10), pixels);
    }

    #[test]
    fn kitty_round_trip() {
        // Noise doesn't compress, so this needs several chunks
        let mut state = 0x2545_f491_u32;
        let pixels: Vec<u8> = (0..200 * 150 * 4)
            .map(|_| {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                state as u8
            })
            .collect();
        let frame = Frame {
            width: 200,
            height: 150,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        let placement = KittyPlacement {
            image_id: 7,
            virtual_cells: None,
        };
        encode_kitty(frame, placement, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();

        let commands: Vec<&str> = text
            .split("\x1b\\")
            .filter(|c| !c.is_empty())
            .map(|c| c.strip_prefix("\x1b_G").unwrap())
            .collect();
        assert!(commands.len() > 1);
        assert!(commands[0].starts_with("a=T,f=32,o=z,s=200,v=150,i=7,p=1,"));

        let mut payload = String::new();
        for (index, command) in commands.iter().enumerate() {
            let (keys, data) = command.split_once(';').unwrap();
            let last = index + 1 == commands.len();
            assert!(keys.ends_with(if last { "m=0" } else { "m=1" }));
            assert!(data.len() <= KITTY_CHUNK_SIZE);
            payload.push_str(data);
        }

        let compressed = BASE64.decode(payload).unwrap();
        let mut decoded = Vec::new();
        flate2::read::ZlibDecoder::new(&compressed[..])
            .read_to_end(&mut decoded)
            .unwrap();
        assert_eq!(decoded, pixels);
    }

    #[test]
    fn kitty_virtual_placement() {
        let pixels = vec![0; 3 * 2 * 4];
        let frame = Frame {
            width: 3,
            height: 2,
            pixels: &pixels,
        };
        let placement = KittyPlacement {
            image_id: 7,
            virtual_cells: Some((4, 3)),
        };
        let mut out = Vec::new();
        encode_kitty_shared(frame, placement, "/dcat-1-7", &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.starts_with("\x1b_Ga=T,f=32,t=s,s=3,v=2,S=24,i=7,p=1,q=2,U=1,c=4,r=3;"));
    }

    #[test]
    fn kitty_placeholders() {
        let placeholder = |cell: &vt100::Cell| cell.contents().to_string();
        let mut out = Vec::new();
        encode_kitty_placeholders(42, (3, 2), &mut out).unwrap();
        let mut parser = vt100::Parser::new(
            std::num::NonZeroU16::new(4).unwrap(),
            std::num::NonZeroU16::new(5).unwrap(),
            0,
        );
        parser.process(&out);
        let screen = parser.screen();
        for (row, diacritic) in [(0, '\u{0305}'), (1, '\u{030d}')] {
            let first = screen.cell(row, 0).unwrap();
            assert_eq!(placeholder(first), format!("\u{10EEEE}{diacritic}\u{0305}"));
            assert_eq!(first.fgcolor(), vt100::Color::Idx(42));
            for col in 1..3 {
                assert_eq!(placeholder(screen.cell(row, col).unwrap()), "\u{10EEEE}");
            }
            assert_eq!(placeholder(screen.cell(row, 3).unwrap()), "");
        }

        // Ids above 255 go in truecolor, with their top byte as a third diacritic
        let mut out = Vec::new();
        encode_kitty_placeholders(0x0201_0203, (1, 1), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.starts_with("\x1b[38;2;1;2;3m"), "{text:?}");
        assert!(
            text.contains("\u{10EEEE}\u{0305}\u{0305}\u{030e}"),
            "{text:?}"
        );
    }

    #[test]
    fn tmux_passthrough() {
        let mut out = Vec::new();
        wrap_for_tmux(b"\x1b_Ga=T;AAAA\x1b\\\x1b_Gm=0;BB\x1b\\", &mut out);
        assert_eq!(
            out,
            b"\x1bPtmux;\x1b\x1b_Ga=T;AAAA\x1b\x1b\\\x1b\\\x1bPtmux;\x1b\x1b_Gm=0;BB\x1b\x1b\\\x1b\\"
        );
    }

    #[test]
    fn kitty_shared_memory_command() {
        let pixels = vec![0; 3 * 2 * 4];
        let frame = Frame {
            width: 3,
            height: 2,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        let placement = KittyPlacement {
            image_id: 7,
            virtual_cells: None,
        };
        encode_kitty_shared(frame, placement, "/dcat-1-7", &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        let command = text
            .strip_prefix("\x1b_G")
            .and_then(|t| t.strip_suffix("\x1b\\"))
            .unwrap();
        let (keys, payload) = command.split_once(';').unwrap();
        assert_eq!(keys, "a=T,f=32,t=s,s=3,v=2,S=24,i=7,p=1,q=2,C=1");
        assert_eq!(BASE64.decode(payload).unwrap(), b"/dcat-1-7");
    }

    // Decode sixel data into palette indices, for checking the encoder
    fn decode_sixel(data: &str, width: usize, height: usize) -> Vec<Option<usize>> {
        let body = data
            .strip_prefix(&format!("\x1bPq\"1;1;{width};{height}"))
            .unwrap()
            .strip_suffix("\x1b\\")
            .unwrap();
        let mut image = vec![None; width * height];
        let (mut x, mut band, mut color) = (0, 0, 0);
        let bytes = body.as_bytes();
        let mut i = 0;
        let number = |i: &mut usize| {
            let start = *i;
            while *i < bytes.len() && bytes[*i].is_ascii_digit() {
                *i += 1;
            }
            body[start..*i].parse::<usize>().unwrap()
        };
        while i < bytes.len() {
            match bytes[i] {
                b'#' => {
                    i += 1;
                    color = number(&mut i);
                    if bytes.get(i) == Some(&b';') {
                        // Palette definition
                        while i < bytes.len() && (bytes[i] == b';' || bytes[i].is_ascii_digit()) {
                            i += 1;
                        }
                    }
                }
                b'$' => {
                    x = 0;
                    i += 1;
                }
                b'-' => {
                    x = 0;
                    band += 1;
                    i += 1;
                }
                b'!' | b'?'..=b'~' => {
                    let run = if bytes[i] == b'!' {
                        i += 1;
                        number(&mut i)
                    } else {
                        1
                    };
                    let bits = bytes[i] - b'?';
                    i += 1;
                    for _ in 0..run {
                        for row in 0..6 {
                            let y = band * 6 + row;
                            if bits & (1 << row) != 0 && y < height {
                                assert!(image[y * width + x].is_none(), "pixel drawn twice");
                                image[y * width + x] = Some(color);
                            }
                        }
                        x += 1;
                    }
                }
                other => panic!("unexpected byte {other}"),
            }
        }
        image
    }

    #[test]
    fn sixel_round_trip() {
        // A gradient across several bands, with a partial last band
        let (width, height) = (37usize, 15usize);
        let pixels: Vec<u8> = (0..width * height)
            .flat_map(|i| {
                let (x, y) = (i % width, i / width);
                [(x * 7) as u8, (y * 17) as u8, 128, 255]
            })
            .collect();
        let frame = Frame {
            width: width as u32,
            height: height as u32,
            pixels: &pixels,
        };

        let mut out = Vec::new();
        encode_sixel(frame, &mut out).unwrap();
        let decoded = decode_sixel(&String::from_utf8(out).unwrap(), width, height);

        let expected = sixel_quantize(frame);
        for (pixel, (decoded, expected)) in decoded.iter().zip(&expected).enumerate() {
            assert_eq!(*decoded, Some(*expected as usize), "pixel {pixel}");
        }
    }

    #[test]
    fn sixel_quantization_is_close() {
        // Flat colors exactly on the palette survive dithering unchanged
        let pixels = [0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255];
        let frame = Frame {
            width: 3,
            height: 1,
            pixels: &pixels,
        };
        let colors: Vec<[u8; 3]> = sixel_quantize(frame)
            .iter()
            .map(|&i| sixel_palette_color(i as usize))
            .collect();
        assert_eq!(colors, vec![[0, 0, 0], [255, 255, 255], [255, 0, 0]]);
    }

    #[test]
    fn sixel_runs() {
        let mut out = Vec::new();
        write_sixel_runs(&[1, 1, 1, 1, 1, 2, 2, 0], &mut out).unwrap();
        assert_eq!(String::from_utf8(out).unwrap(), "!5@AA?");
    }
}

#[cfg(test)]
mod bench {
    use super::*;

    #[test]
    #[ignore = "timing only; run with --release --ignored"]
    fn kitty_encode_time() {
        for (width, height) in [(1200u32, 800u32), (1920, 1080), (2560, 1440), (3840, 2160)] {
            // A rendered-looking frame: flat background with a smooth blob
            let pixels: Vec<u8> = (0..width * height)
                .flat_map(|i| {
                    let (x, y) = (
                        (i % width) as f32 / width as f32,
                        (i / width) as f32 / height as f32,
                    );
                    let d = ((x - 0.5).powi(2) + (y - 0.5).powi(2)).sqrt();
                    if d < 0.3 {
                        [
                            (200.0 * (1.0 - d)) as u8,
                            (180.0 * y) as u8,
                            (150.0 * x) as u8,
                            255,
                        ]
                    } else {
                        [0, 0, 0, 255]
                    }
                })
                .collect();
            let frame = Frame {
                width,
                height,
                pixels: &pixels,
            };
            let mut out = Vec::new();
            let start = std::time::Instant::now();
            for _ in 0..5 {
                out.clear();
                let placement = KittyPlacement {
                    image_id: 1,
                    virtual_cells: None,
                };
                encode_kitty(frame, placement, &mut out).unwrap();
            }
            eprintln!(
                "kitty {width}x{height}: {:?} per frame, {} KB",
                start.elapsed() / 5,
                out.len() / 1024
            );
        }
    }

    #[test]
    #[ignore = "timing only; run with --release --ignored"]
    fn sixel_encode_time() {
        // A real frame if one is given (binary PPM), otherwise a synthetic gradient
        let (width, height, pixels) = match std::env::var("DCAT_BENCH_PPM") {
            Ok(path) => {
                let data = std::fs::read(path).unwrap();
                let header: Vec<&[u8]> = data.splitn(5, |b| b.is_ascii_whitespace()).collect();
                let parse = |b: &[u8]| std::str::from_utf8(b).unwrap().parse::<u32>().unwrap();
                let (w, h) = (parse(header[1]), parse(header[2]));
                let rgb = &data[data.len() - (w * h * 3) as usize..];
                let rgba: Vec<u8> = rgb
                    .as_chunks::<3>()
                    .0
                    .iter()
                    .flat_map(|&[r, g, b]| [r, g, b, 255])
                    .collect();
                (w, h, rgba)
            }
            Err(_) => (
                1200,
                780,
                (0..1200 * 780 * 4).map(|i| (i % 200) as u8).collect(),
            ),
        };
        let frame = Frame {
            width,
            height,
            pixels: &pixels,
        };

        let start = std::time::Instant::now();
        for _ in 0..10 {
            std::hint::black_box(sixel_quantize(frame));
        }
        let quantize = start.elapsed() / 10;

        let mut out = Vec::new();
        let start = std::time::Instant::now();
        for _ in 0..10 {
            out.clear();
            encode_sixel(frame, &mut out).unwrap();
        }
        eprintln!(
            "sixel {width}x{height}: quantize {quantize:?}, total {:?} per frame, {} bytes",
            start.elapsed() / 10,
            out.len()
        );
    }
}
