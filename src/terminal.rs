pub fn draw(pixels: Vec<u8>, width: u32, height: u32) {
    for row in (0..height).step_by(2) {
        for col in 0..width {
            let start_top = (row as usize * width as usize + col as usize) * 3;
            let start_bottom = ((row as usize + 1) * width as usize + col as usize) * 3;
            print!(
                "\x1b[38;2;{};{};{};48;2;{};{};{}m▀",
                pixels[start_top],
                pixels[start_top + 1],
                pixels[start_top + 2],
                pixels[start_bottom],
                pixels[start_bottom + 1],
                pixels[start_bottom + 2]
            );
        }
    }
}
