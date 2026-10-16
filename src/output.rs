use std::io::{self, Write};

use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use crossterm::{
    cursor::MoveTo,
    queue,
    style::{Color, Colors, Print, ResetColor, SetColors},
};
use flate2::{Compression, write::ZlibEncoder};

use crate::cli::OutputMode;

// A rendered frame of tightly packed RGB pixels
#[derive(Clone, Copy)]
pub struct Frame<'a> {
    pub width: u32,
    pub height: u32,
    pub pixels: &'a [u8],
}

impl Frame<'_> {
    fn pixel(&self, x: usize, y: usize) -> [u8; 3] {
        let start = (y * self.width as usize + x) * 3;
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
        let row_bytes = self.width as usize * 3;
        self.pixels
            .chunks_exact(row_bytes)
            .take(height)
            .flat_map(|row| &row[..width * 3])
            .copied()
            .collect()
    }
}

impl OutputMode {
    // Whether the mode draws with text characters rather than images
    pub fn uses_characters(self) -> bool {
        !matches!(self, Self::Kitty | Self::Sixel)
    }
}

// Pick the best output mode the terminal is known to support, judging by its
// environment variables
pub fn detect(env: impl Fn(&str) -> Option<String>) -> OutputMode {
    let term = env("TERM").unwrap_or_default();
    let term_program = env("TERM_PROGRAM").unwrap_or_default();
    let colorterm = env("COLORTERM").unwrap_or_default();

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

    if matches!(colorterm.as_str(), "truecolor" | "24bit")
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

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ColorDepth {
    Truecolor,
    // The 240 colors of the xterm palette that don't depend on the theme
    Palette,
}

// Two pixels per cell: the upper as the foreground of '▀', the lower as the background
pub fn encode_half_blocks(
    frame: Frame,
    cols: usize,
    rows: usize,
    depth: ColorDepth,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let to_color = |[r, g, b]: [u8; 3]| match depth {
        ColorDepth::Truecolor => Color::Rgb { r, g, b },
        ColorDepth::Palette => Color::AnsiValue(nearest_palette_index([r, g, b])),
    };
    let cols = cols.min(frame.width as usize);
    let rows = rows.min(frame.height.div_ceil(2) as usize);
    let height = frame.height as usize;

    // Colors persist between cells, so only emit them when they change
    let mut current = None;
    for row in 0..rows {
        queue!(out, MoveTo(0, row as u16))?;
        let (top, bottom) = (row * 2, row * 2 + 1);
        for col in 0..cols {
            let foreground = to_color(frame.pixel(col, top));
            // An odd height leaves the last cell without a bottom pixel
            let background = if bottom < height {
                to_color(frame.pixel(col, bottom))
            } else {
                Color::Reset
            };
            let colors = (foreground, background);
            if current != Some(colors) {
                queue!(out, SetColors(Colors::new(foreground, background)))?;
                current = Some(colors);
            }
            queue!(out, Print('▀'))?;
        }
    }
    queue!(out, ResetColor)
}

// 4x4 ordered dither thresholds, in sixteenths
const BAYER_4X4: [[u8; 4]; 4] = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];

fn dither_threshold(x: usize, y: usize) -> f32 {
    (f32::from(BAYER_4X4[y % 4][x % 4]) + 0.5) / 16.0
}

// Monochrome half blocks in the terminal's own foreground and background
// colors, with dithering to show shading
pub fn encode_mono_blocks(
    frame: Frame,
    cols: usize,
    rows: usize,
    out: &mut Vec<u8>,
) -> io::Result<()> {
    let cols = cols.min(frame.width as usize);
    let rows = rows.min(frame.height.div_ceil(2) as usize);
    let height = frame.height as usize;
    let lit = |x: usize, y: usize| {
        y < height && {
            let [r, g, b] = frame.pixel(x, y);
            let luminance =
                (0.2126 * f32::from(r) + 0.7152 * f32::from(g) + 0.0722 * f32::from(b)) / 255.0;
            luminance > dither_threshold(x, y)
        }
    };

    queue!(out, ResetColor)?;
    for row in 0..rows {
        queue!(out, MoveTo(0, row as u16))?;
        let line: String = (0..cols)
            .map(|col| match (lit(col, row * 2), lit(col, row * 2 + 1)) {
                (false, false) => ' ',
                (true, false) => '▀',
                (false, true) => '▄',
                (true, true) => '█',
            })
            .collect();
        queue!(out, Print(line))?;
    }
    Ok(())
}

// The xterm 256-color palette above the 16 theme colors: a 6x6x6 color cube
// (16-231) and a 24-step gray ramp (232-255)
const CUBE_LEVELS: [u8; 6] = [0, 95, 135, 175, 215, 255];

pub fn nearest_palette_index([r, g, b]: [u8; 3]) -> u8 {
    let nearest_level = |value: u8| {
        CUBE_LEVELS
            .iter()
            .enumerate()
            .min_by_key(|&(_, &level)| value.abs_diff(level))
            .map_or(0, |(index, _)| index)
    };
    let distance = |[r2, g2, b2]: [u8; 3]| {
        let d = |a: u8, b: u8| u32::from(a.abs_diff(b)).pow(2);
        d(r, r2) + d(g, g2) + d(b, b2)
    };

    let (ri, gi, bi) = (nearest_level(r), nearest_level(g), nearest_level(b));
    let cube_color = [CUBE_LEVELS[ri], CUBE_LEVELS[gi], CUBE_LEVELS[bi]];
    let cube_index = 16 + 36 * ri + 6 * gi + bi;

    let average = (u32::from(r) + u32::from(g) + u32::from(b)) / 3;
    let gray_step = (average.saturating_sub(3) / 10).min(23);
    let gray_level = (8 + 10 * gray_step) as u8;
    let gray_index = 232 + gray_step as usize;

    if distance([gray_level; 3]) < distance(cube_color) {
        gray_index as u8
    } else {
        cube_index as u8
    }
}

// Escape sequences are limited to this many bytes of base64 payload each
const KITTY_CHUNK_SIZE: usize = 4096;

// Kitty graphics protocol: compressed RGB, replacing the previous frame's
// image and placement so they don't pile up. The cursor isn't moved, so a
// full-height image can't scroll the screen.
pub fn encode_kitty(frame: Frame, out: &mut Vec<u8>) -> io::Result<()> {
    let mut encoder = ZlibEncoder::new(Vec::new(), Compression::fast());
    encoder.write_all(frame.pixels)?;
    let payload = BASE64.encode(encoder.finish()?);

    let chunks: Vec<&[u8]> = payload.as_bytes().chunks(KITTY_CHUNK_SIZE).collect();
    for (index, chunk) in chunks.iter().enumerate() {
        let more = u8::from(index + 1 < chunks.len());
        if index == 0 {
            write!(
                out,
                "\x1b_Ga=T,f=24,o=z,s={},v={},i=1,p=1,q=2,C=1,m={more};",
                frame.width, frame.height
            )?;
        } else {
            write!(out, "\x1b_Gm={more};")?;
        }
        out.extend_from_slice(chunk);
        out.extend_from_slice(b"\x1b\\");
    }
    Ok(())
}

// Removes every Kitty image, so none are left behind on exit
pub const KITTY_DELETE_ALL: &str = "\x1b_Ga=d,d=A,q=2\x1b\\";

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
    for (y, row) in frame.pixels.chunks_exact(width * 3).enumerate() {
        let thresholds = &BAYER_4X4[y % 4];
        for (x, [r, g, b]) in row.as_chunks::<3>().0.iter().enumerate() {
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
            (&[("TERM", "linux")], OutputMode::BlockCharacters),
        ];
        for (vars, expected) in cases {
            assert_eq!(detect(env(vars)), *expected, "{vars:?}");
        }
        if !cfg!(windows) {
            assert_eq!(
                detect(env(&[("TERM", "xterm-256color")])),
                OutputMode::PaletteCharacters
            );
            assert_eq!(detect(env(&[])), OutputMode::PaletteCharacters);
        }
    }

    #[test]
    fn palette_indices() {
        assert_eq!(nearest_palette_index([0, 0, 0]), 16);
        assert_eq!(nearest_palette_index([255, 255, 255]), 231);
        assert_eq!(nearest_palette_index([255, 0, 0]), 196);
        assert_eq!(nearest_palette_index([128, 128, 128]), 244);
        assert_eq!(nearest_palette_index([95, 135, 175]), 16 + 36 + 2 * 6 + 3);
    }

    fn frame_from(width: u32, height: u32, pixels: &[[u8; 3]]) -> Vec<u8> {
        assert_eq!(pixels.len(), (width * height) as usize);
        pixels.concat()
    }

    #[test]
    fn half_blocks_repeat_colors_only_when_changed() {
        let red = [255, 0, 0];
        let blue = [0, 0, 255];
        let pixels = frame_from(3, 2, &[red, red, red, blue, blue, blue]);
        let mut out = Vec::new();
        let frame = Frame {
            width: 3,
            height: 2,
            pixels: &pixels,
        };
        encode_half_blocks(frame, 3, 1, ColorDepth::Truecolor, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert_eq!(text.matches('▀').count(), 3);
        assert_eq!(text.matches("38;2;255;0;0").count(), 1);
    }

    #[test]
    fn half_blocks_clip_and_handle_odd_height() {
        let pixels = vec![200; 4 * 3 * 3];
        let frame = Frame {
            width: 4,
            height: 3,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        encode_half_blocks(frame, 2, 10, ColorDepth::Palette, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        // Two columns of two rows, the last row without a bottom pixel
        assert_eq!(text.matches('▀').count(), 4);
        assert!(text.contains("48;5;"));
    }

    #[test]
    fn mono_blocks() {
        let white = [255; 3];
        let black = [0; 3];
        let pixels = frame_from(
            4,
            2,
            &[white, white, black, black, black, white, black, white],
        );
        let frame = Frame {
            width: 4,
            height: 2,
            pixels: &pixels,
        };
        let mut out = Vec::new();
        encode_mono_blocks(frame, 4, 1, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.ends_with("▀█ ▄"), "{text:?}");
    }

    #[test]
    fn crop() {
        let pixels: Vec<u8> = (0..4 * 2 * 3).map(|v| v as u8).collect();
        let frame = Frame {
            width: 4,
            height: 2,
            pixels: &pixels,
        };
        assert_eq!(frame.cropped(2, 1), vec![0, 1, 2, 3, 4, 5]);
        assert_eq!(frame.cropped(10, 10), pixels);
    }

    #[test]
    fn kitty_round_trip() {
        // Noise doesn't compress, so this needs several chunks
        let mut state = 0x2545_f491_u32;
        let pixels: Vec<u8> = (0..200 * 150 * 3)
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
        encode_kitty(frame, &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();

        let commands: Vec<&str> = text
            .split("\x1b\\")
            .filter(|c| !c.is_empty())
            .map(|c| c.strip_prefix("\x1b_G").unwrap())
            .collect();
        assert!(commands.len() > 1);
        assert!(commands[0].starts_with("a=T,f=24,o=z,s=200,v=150,"));

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
                [(x * 7) as u8, (y * 17) as u8, 128]
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
        let pixels = [0, 0, 0, 255, 255, 255, 255, 0, 0];
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
    fn sixel_encode_time() {
        // A real frame if one is given (binary PPM), otherwise a synthetic gradient
        let (width, height, pixels) = match std::env::var("DCAT_BENCH_PPM") {
            Ok(path) => {
                let data = std::fs::read(path).unwrap();
                let header: Vec<&[u8]> = data.splitn(5, |b| b.is_ascii_whitespace()).collect();
                let parse = |b: &[u8]| std::str::from_utf8(b).unwrap().parse::<u32>().unwrap();
                let (w, h) = (parse(header[1]), parse(header[2]));
                (w, h, data[data.len() - (w * h * 3) as usize..].to_vec())
            }
            Err(_) => (
                1200,
                780,
                (0..1200 * 780 * 3).map(|i| (i % 200) as u8).collect(),
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
