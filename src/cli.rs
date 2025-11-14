use clap::Parser;
use std::path::PathBuf;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
pub struct Args {
    /// Path to the model file
    #[arg(value_name = "MODEL")]
    pub model: PathBuf,

    /// Path to the texture file (defaults to gray)
    #[arg(short, long)]
    pub texture: Option<PathBuf>,

    /// Path to normal image file
    #[arg(short = 'n', long)]
    pub normal_map: Option<PathBuf>,

    /// Renderer width (defaults to terminal width)
    #[arg(short = 'W', long)]
    pub width: Option<u32>,

    /// Renderer width (defaults to terminal height)
    #[arg(short = 'H', long)]
    pub height: Option<u32>,

    /// Camera distance from origin (defaults to auto-calculated based on model size)
    #[arg(long)]
    pub camera_distance: Option<f32>,

    /// Enable Sixel graphics mode
    #[arg(short = 'S', long)]
    pub sixel: bool,

    /// Enable Kitty graphics protocol mode
    #[arg(short = 'K', long)]
    pub kitty: bool,

    /// Enable Kitty graphics protocol mode
    #[arg(short = 'P', long)]
    pub pixels: bool,

    /// Disable lighting calculations
    #[arg(long)]
    pub no_lighting: bool,
}
