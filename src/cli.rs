use std::path::PathBuf;

use clap::{ArgGroup, Parser, value_parser};
use termblit::OutputMode;

pub const CONTROLS: &str = "\
Controls:
q - Quit
m - Toggle wireframe
Animation:
p - Play/pause animation
1 - Previous animation
2 - Next animation

FPS controls:
wasd - Move
ijkl - Look around
space - Move up
shift - Move down
v - slow down
b - speed up
Regular keyboard controls:
wasd - rotate around the model
e - zoom in
r - zoom out
";

/// A 3D model viewer for the terminal
#[derive(Parser, Debug)]
#[command(version, about)]
#[command(group(ArgGroup::new("output_mode").multiple(false)))]
#[command(after_help = "With no output-mode flag, the best supported terminal format is selected.")]
pub struct Args {
    /// Path to the model file
    #[arg(required_unless_present_any = ["controls", "detect_output"])]
    pub model_path: Option<PathBuf>,

    /// Path to the texture file
    #[arg(short, long, value_name = "PATH")]
    pub texture: Option<PathBuf>,

    /// Path to the normal map file
    #[arg(short, long, value_name = "PATH")]
    pub normal_map: Option<PathBuf>,

    /// Renderer width in pixels
    #[arg(short = 'W', long, requires = "height", value_parser = value_parser!(u32).range(1..=65535), allow_negative_numbers = true)]
    pub width: Option<u32>,

    /// Renderer height in pixels
    #[arg(short = 'H', long, requires = "width", value_parser = value_parser!(u32).range(1..=65535), allow_negative_numbers = true)]
    pub height: Option<u32>,

    /// Pixels per terminal cell, like 9x18, for terminals that don't report it
    #[arg(long, value_name = "WxH", value_parser = cell_size, conflicts_with_all = ["width", "height"])]
    pub cell_size: Option<(u32, u32)>,

    /// Camera distance from origin
    #[arg(long, value_name = "DIST", value_parser = positive_f32, allow_negative_numbers = true)]
    pub camera_distance: Option<f32>,

    /// Scale multiplier for the model
    #[arg(long, value_name = "SCALE", default_value_t = 1.0, value_parser = positive_f32, allow_negative_numbers = true)]
    pub model_scale: f32,

    /// Spin the model at the given speed (rad/s)
    #[arg(long, value_name = "SPEED", default_value_t = 0.0, value_parser = finite_f32, allow_negative_numbers = true)]
    pub spin: f32,

    /// Target frames per second
    #[arg(short, long, default_value_t = 60, value_parser = value_parser!(u32).range(1..), allow_negative_numbers = true)]
    pub fps: u32,

    /// Disable lighting calculations
    #[arg(long)]
    pub no_lighting: bool,

    /// Enable first-person camera controls
    #[arg(long)]
    pub keyboard_controls: bool,

    /// With --keyboard-controls, read held keys from the keyboard itself when
    /// the terminal can't report releases, like Neovim's or tmux. Linux and
    /// macOS only, and it needs permission to read every keystroke: the input
    /// group on Linux, Input Monitoring on macOS.
    #[arg(long, requires = "keyboard_controls")]
    pub raw_keyboard: bool,

    /// Enable mouse drag to orbit the model
    #[arg(long)]
    pub mouse_orbit: bool,

    /// Mouse drag sensitivity
    #[arg(long, value_name = "S", default_value_t = 0.02, value_parser = positive_f32, allow_negative_numbers = true)]
    pub mouse_sensitivity: f32,

    /// Show status bar
    #[arg(short, long)]
    pub status_bar: bool,

    /// Use Sixel graphics
    #[arg(short = 'S', long, group = "output_mode")]
    pub sixel: bool,

    /// Use the Kitty graphics protocol
    #[arg(
        short = 'K',
        long,
        visible_alias = "kitty-direct",
        group = "output_mode"
    )]
    pub kitty: bool,

    /// Send Kitty frames through shared memory without asking the terminal
    /// first. Falls back to inline frames if the terminal doesn't read them.
    #[arg(long)]
    pub kitty_shared_memory: bool,

    /// Kitty image id, so dcat's image doesn't replace another program's
    /// [default: 1, or none for --once]
    #[arg(long, value_name = "ID", value_parser = value_parser!(u32).range(1..))]
    pub kitty_image_id: Option<u32>,

    /// Use truecolor characters
    #[arg(short = 'T', long, group = "output_mode")]
    pub truecolor_characters: bool,

    /// Use 240-color palette characters
    #[arg(short = 'P', long, group = "output_mode")]
    pub palette_characters: bool,

    /// Use monochrome block characters
    #[arg(short = 'B', long, group = "output_mode")]
    pub block_characters: bool,

    /// Print a single frame at the cursor and exit
    #[arg(long)]
    pub once: bool,

    /// Save a single frame as an image instead, in the format its extension
    /// names (png, jpg, webp, ...), 1024x1024 unless -W and -H are given
    #[arg(long, value_name = "PATH", conflicts_with = "once")]
    pub image: Option<PathBuf>,

    /// Display controls
    #[arg(long)]
    pub controls: bool,

    /// Print the output mode that would be used (kitty, sixel, truecolor,
    /// palette or block) and exit
    #[arg(long)]
    pub detect_output: bool,
}

impl Args {
    // The output mode forced on the command line, if any
    pub fn output_mode(&self) -> Option<OutputMode> {
        [
            (self.sixel, OutputMode::Sixel),
            (self.kitty, OutputMode::Kitty),
            (self.truecolor_characters, OutputMode::TruecolorCharacters),
            (self.palette_characters, OutputMode::PaletteCharacters),
            (self.block_characters, OutputMode::BlockCharacters),
        ]
        .into_iter()
        .find_map(|(set, mode)| set.then_some(mode))
    }

    // The forced output mode, or else the best the terminal supports
    pub fn chosen_output_mode(&self) -> OutputMode {
        self.output_mode().unwrap_or_else(termblit::detect)
    }

    pub fn terminal_options(&self) -> termblit::Options {
        termblit::Options {
            cell_size: self.cell_size,
            kitty_image_id: self.kitty_image_id,
            kitty_shared_memory: self.kitty_shared_memory,
            mouse_capture: self.mouse_orbit,
            key_releases: self.keyboard_controls,
        }
    }
}

fn cell_size(value: &str) -> Result<(u32, u32), String> {
    let invalid = || format!("expected WIDTHxHEIGHT in pixels, like 9x18: {value}");
    let (width, height) = value.split_once('x').ok_or_else(invalid)?;
    match (width.parse(), height.parse()) {
        (Ok(width), Ok(height)) if width > 0 && height > 0 => Ok((width, height)),
        _ => Err(invalid()),
    }
}

fn finite_f32(value: &str) -> Result<f32, String> {
    let parsed: f32 = value
        .parse()
        .map_err(|_| format!("invalid number: {value}"))?;
    if !parsed.is_finite() {
        return Err(format!("number must be finite: {value}"));
    }
    Ok(parsed)
}

fn positive_f32(value: &str) -> Result<f32, String> {
    let parsed = finite_f32(value)?;
    if parsed <= 0.0 {
        return Err(format!("must be greater than 0: {value}"));
    }
    Ok(parsed)
}

#[cfg(test)]
mod tests {
    use super::*;
    use clap::{CommandFactory, error::ErrorKind};

    fn parse(args: &[&str]) -> Result<Args, clap::Error> {
        Args::try_parse_from(std::iter::once("dcat").chain(args.iter().copied()))
    }

    fn parse_err(args: &[&str]) -> ErrorKind {
        parse(args).unwrap_err().kind()
    }

    #[test]
    fn command_is_well_formed() {
        Args::command().debug_assert();
    }

    #[test]
    fn defaults() {
        let args = parse(&["model.glb"]).unwrap();
        assert_eq!(args.model_path, Some(PathBuf::from("model.glb")));
        assert_eq!(args.width, None);
        assert_eq!(args.height, None);
        assert_eq!(args.camera_distance, None);
        assert_eq!(args.model_scale, 1.0);
        assert_eq!(args.spin, 0.0);
        assert_eq!(args.fps, 60);
        assert_eq!(args.mouse_sensitivity, 0.02);
        assert!(!args.status_bar);
        assert!(!args.kitty_shared_memory);
        assert_eq!(args.kitty_image_id, None);
        assert_eq!(args.output_mode(), None);
    }

    #[test]
    fn all_options() {
        let args = parse(&[
            "-t",
            "tex.png",
            "-n",
            "normal.png",
            "-W",
            "320",
            "-H",
            "240",
            "--camera-distance",
            "4.5",
            "--model-scale",
            "2",
            "--spin",
            "-1.5",
            "-f",
            "30",
            "--no-lighting",
            "--keyboard-controls",
            "--mouse-orbit",
            "--mouse-sensitivity",
            "0.1",
            "-s",
            "--kitty-shared-memory",
            "--kitty-image-id",
            "2147483000",
            "model.glb",
        ])
        .unwrap();
        assert_eq!(args.texture, Some(PathBuf::from("tex.png")));
        assert_eq!(args.normal_map, Some(PathBuf::from("normal.png")));
        assert_eq!((args.width, args.height), (Some(320), Some(240)));
        assert_eq!(args.camera_distance, Some(4.5));
        assert_eq!(args.model_scale, 2.0);
        assert_eq!(args.spin, -1.5);
        assert_eq!(args.fps, 30);
        assert!(args.no_lighting && args.keyboard_controls && args.mouse_orbit && args.status_bar);
        assert_eq!(args.mouse_sensitivity, 0.1);
        assert!(args.kitty_shared_memory);
        assert_eq!(args.kitty_image_id, Some(2147483000));
    }

    #[test]
    fn output_modes() {
        for (flag, mode) in [
            ("-S", OutputMode::Sixel),
            ("--kitty", OutputMode::Kitty),
            ("--kitty-direct", OutputMode::Kitty),
            ("-T", OutputMode::TruecolorCharacters),
            ("-P", OutputMode::PaletteCharacters),
            ("-B", OutputMode::BlockCharacters),
        ] {
            assert_eq!(
                parse(&[flag, "m.glb"]).unwrap().output_mode(),
                Some(mode),
                "{flag}"
            );
        }
    }

    #[test]
    fn conflicting_output_modes() {
        assert_eq!(
            parse_err(&["-S", "-K", "m.glb"]),
            ErrorKind::ArgumentConflict
        );
        assert_eq!(
            parse_err(&["-T", "-B", "m.glb"]),
            ErrorKind::ArgumentConflict
        );
    }

    #[test]
    fn model_required_unless_controls() {
        assert_eq!(parse_err(&[]), ErrorKind::MissingRequiredArgument);
        assert!(parse(&["--controls"]).unwrap().controls);
        assert!(parse(&["--detect-output"]).unwrap().detect_output);
        assert_eq!(
            parse(&["--image", "out.png", "m.glb"]).unwrap().image,
            Some(PathBuf::from("out.png"))
        );
        assert_eq!(
            parse_err(&["--image", "out.png", "--once", "m.glb"]),
            ErrorKind::ArgumentConflict
        );
    }

    #[test]
    fn raw_keyboard_needs_keyboard_controls() {
        assert_eq!(
            parse_err(&["--raw-keyboard", "m.glb"]),
            ErrorKind::MissingRequiredArgument
        );
        let args = parse(&["--keyboard-controls", "--raw-keyboard", "m.glb"]).unwrap();
        assert!(args.raw_keyboard);
    }

    #[test]
    fn width_and_height_go_together() {
        assert_eq!(
            parse_err(&["-W", "100", "m.glb"]),
            ErrorKind::MissingRequiredArgument
        );
        assert_eq!(
            parse_err(&["-H", "100", "m.glb"]),
            ErrorKind::MissingRequiredArgument
        );
    }

    #[test]
    fn cell_size() {
        assert_eq!(parse(&["m.glb"]).unwrap().cell_size, None);
        assert_eq!(
            parse(&["--cell-size", "9x18", "m.glb"]).unwrap().cell_size,
            Some((9, 18))
        );
        // It sizes the frame from the terminal, which a fixed size overrides
        assert_eq!(
            parse_err(&["--cell-size", "9x18", "-W", "10", "-H", "10", "m.glb"]),
            ErrorKind::ArgumentConflict
        );
    }

    #[test]
    fn invalid_values() {
        for args in [
            &["-W", "0", "-H", "10", "m.glb"][..],
            &["-W", "65536", "-H", "10", "m.glb"],
            &["-W", "abc", "-H", "10", "m.glb"],
            &["-f", "0", "m.glb"],
            &["--model-scale", "0", "m.glb"],
            &["--model-scale", "-1", "m.glb"],
            &["--camera-distance", "0", "m.glb"],
            &["--mouse-sensitivity", "-0.5", "m.glb"],
            &["-f", "-5", "m.glb"],
            &["-W", "-1", "-H", "10", "m.glb"],
            &["--spin", "inf", "m.glb"],
            &["--spin", "nan", "m.glb"],
            &["--cell-size", "9", "m.glb"],
            &["--cell-size", "0x18", "m.glb"],
            &["--cell-size", "9x-18", "m.glb"],
            &["--cell-size", "9x18x2", "m.glb"],
            &["--kitty-image-id", "0", "m.glb"],
        ] {
            assert_eq!(parse_err(args), ErrorKind::ValueValidation, "{args:?}");
        }
    }

    #[test]
    fn unknown_option() {
        assert_eq!(parse_err(&["--bogus", "m.glb"]), ErrorKind::UnknownArgument);
    }
}
