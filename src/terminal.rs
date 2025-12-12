use sixel_rs::sys::PixelFormat;
use std::ffi::{c_uchar, c_void};
use std::io::Write;
use std::{ptr, slice};
use termion::{terminal_size, terminal_size_pixels};

const DEFAULT_TERM_WIDTH: u32 = 80;
const DEFAULT_TERM_HEIGHT: u32 = 48;

pub fn render_terminal(buffer: &[u8], width: u32, height: u32) {
    let num_chars = (width * (height / 2)) as usize;
    let capacity = num_chars * 28 + 100; // Extra for header/footer
    let mut output = String::with_capacity(capacity);

    output.push_str("\x1b[?2026h"); // Begin synchronized update
    output.push_str("\x1b[H"); // Home cursor to 0,0

    let buffer_len = buffer.len();
    for y in (0..height).step_by(2) {
        for x in 0..width {
            let idx_upper = ((y * width + x) * 3) as usize;
            let idx_lower = (((y + 1) * width + x) * 3) as usize;

            let (r_upper, g_upper, b_upper) = if idx_upper + 2 < buffer_len {
                unsafe {
                    (
                        *buffer.get_unchecked(idx_upper),
                        *buffer.get_unchecked(idx_upper + 1),
                        *buffer.get_unchecked(idx_upper + 2),
                    )
                }
            } else {
                (0, 0, 0)
            };

            let (r_lower, g_lower, b_lower) = if idx_lower + 2 < buffer_len && y + 1 < height {
                unsafe {
                    (
                        *buffer.get_unchecked(idx_lower),
                        *buffer.get_unchecked(idx_lower + 1),
                        *buffer.get_unchecked(idx_lower + 2),
                    )
                }
            } else {
                (0, 0, 0)
            };

            use std::fmt::Write;
            let _ = write!(
                output,
                "\x1b[38;2;{};{};{};48;2;{};{};{}m▀", // Print foreground (upper) and background (lower) colors
                r_upper, g_upper, b_upper, r_lower, g_lower, b_lower
            );
        }
    }

    output.push_str("\x1b[0m"); // Clear formatting
    output.push_str("\x1b[?2026l"); // End synchronized update

    print!("{}", output);
}

fn sixel_string_fast(
    bytes: &[u8],
    width: i32,
    height: i32,
) -> Result<String, Box<dyn std::error::Error>> {
    use sixel_rs::sys::{
        DiffusionMethod as SysDiffusionMethod, Dither, EncodePolicy, MethodForLargest,
        MethodForRepColor, Output, QualityMode, sixel_dither_destroy, sixel_dither_initialize,
        sixel_dither_new, sixel_dither_set_diffusion_type, sixel_encode, sixel_output_destroy,
        sixel_output_new, sixel_output_set_encode_policy, status as sixel_status,
    };

    const SIXEL_CALLBACK_ERROR: i32 = 1;
    const DEPTH_BITS: i32 = 24;

    let mut sixel_data: Vec<std::os::raw::c_char> = Vec::new();
    let sixel_data_ptr: *mut c_void = &mut sixel_data as *mut _ as *mut c_void;

    let mut output: *mut Output = ptr::null_mut();
    let output_ptr: *mut *mut Output = &mut output as *mut _;

    let mut dither: *mut Dither = ptr::null_mut();
    let dither_ptr: *mut *mut Dither = &mut dither as *mut _;

    let pixels = bytes.as_ptr() as *mut c_uchar;

    unsafe extern "C" fn callback(
        data: *mut std::os::raw::c_char,
        size: std::os::raw::c_int,
        priv_: *mut c_void,
    ) -> std::os::raw::c_int {
        let sixel_data: &mut Vec<std::os::raw::c_char> =
            unsafe { &mut *(priv_ as *mut Vec<std::os::raw::c_char>) };

        if data.is_null() {
            return SIXEL_CALLBACK_ERROR;
        }

        let data_slice: &[std::os::raw::c_char] =
            unsafe { slice::from_raw_parts(data, size as usize) };
        sixel_data.extend_from_slice(data_slice);
        sixel_status::OK
    }

    unsafe {
        let result = sixel_output_new(output_ptr, Some(callback), sixel_data_ptr, ptr::null_mut());
        if result != sixel_status::OK {
            return Err("Failed to create sixel output".into());
        }

        sixel_output_set_encode_policy(output, EncodePolicy::Fast);

        let result = sixel_dither_new(dither_ptr, 256, ptr::null_mut());
        if result != sixel_status::OK {
            sixel_output_destroy(output);
            return Err("Failed to create sixel dither".into());
        }

        let result = sixel_dither_initialize(
            dither,
            pixels,
            width,
            height,
            PixelFormat::RGB888,
            MethodForLargest::Normal,
            MethodForRepColor::CenterOfBox,
            QualityMode::Low,
        );
        if result != sixel_status::OK {
            sixel_dither_destroy(dither);
            sixel_output_destroy(output);
            return Err("Failed to initialize sixel dither".into());
        }

        sixel_dither_set_diffusion_type(dither, SysDiffusionMethod::None);

        let result = sixel_encode(pixels, width, height, DEPTH_BITS, dither, output);

        sixel_output_destroy(output);
        sixel_dither_destroy(dither);

        if result != sixel_status::OK {
            return Err("Failed to encode sixel".into());
        }

        let bytes: Vec<u8> = sixel_data.into_iter().map(|c| c as u8).collect();
        String::from_utf8(bytes).map_err(|_| "Invalid UTF-8 in sixel data".into())
    }
}

pub fn render_sixel(buffer: &[u8], width: u32, height: u32) {
    let mut stdout = std::io::BufWriter::new(std::io::stdout());
    let _ = write!(stdout, "\x1b[H"); // Home cursor to 0,0
    match sixel_string_fast(buffer, width as i32, height as i32) {
        Ok(sixel) => {
            let _ = write!(stdout, "{}", sixel);
            let _ = stdout.flush();
        }
        Err(e) => {
            eprintln!(
                "Warning: Sixel conversion failed: {}. Your terminal may not support Sixel graphics or the image is too large.",
                e
            );
        }
    }
}

pub fn render_kitty(buffer: &[u8], width: u32, height: u32) {
    use base64::{Engine, engine::general_purpose::STANDARD};

    let mut output = String::with_capacity(32768);

    output.push_str("\x1b[H"); // Home cursor to 0,0

    let encoded = STANDARD.encode(buffer);

    const CHUNK_SIZE: usize = 4096;

    let total_chunks = encoded.len().div_ceil(CHUNK_SIZE);
    let encoded_bytes = encoded.as_bytes();

    for (i, chunk) in encoded_bytes.chunks(CHUNK_SIZE).enumerate() {
        let is_last = i == total_chunks - 1;
        let chunk_str = unsafe { std::str::from_utf8_unchecked(chunk) };

        if i == 0 {
            // First chunk: include image parameters
            output.push_str(&format!(
                "\x1b_Ga=T,f=24,s={},v={},m={};{}\x1b\\",
                width,
                height,
                if is_last { 0 } else { 1 },
                chunk_str
            ));
        } else {
            // Subsequent chunks: only include data
            output.push_str(&format!(
                "\x1b_Gm={};{}\x1b\\",
                if is_last { 0 } else { 1 },
                chunk_str
            ));
        }
    }

    print!("{}", output);
}

pub fn calculate_render_dimensions(
    explicit_width: Option<u32>,
    explicit_height: Option<u32>,
    use_sixel: bool,
    use_kitty: bool,
) -> (u32, u32) {
    if let (Some(w), Some(h)) = (explicit_width, explicit_height) {
        return (w, h);
    }

    let (term_cols, term_rows) = if use_sixel || use_kitty {
        terminal_size_pixels()
            .map(|size| (size.0 as u32, size.1 as u32))
            .unwrap_or((DEFAULT_TERM_WIDTH, DEFAULT_TERM_HEIGHT))
    } else {
        terminal_size()
            .map(|size| (size.0 as u32, size.1 as u32))
            .unwrap_or((DEFAULT_TERM_WIDTH, DEFAULT_TERM_HEIGHT))
    };

    if use_sixel || use_kitty {
        let pixel_width = term_cols;
        let pixel_height = term_rows;
        (pixel_width, pixel_height)
    } else {
        let pixel_width = term_cols;
        let pixel_height = term_rows * 2; // 2 pixels per character (top and bottom)
        (pixel_width, pixel_height)
    }
}

