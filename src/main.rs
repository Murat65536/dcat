use std::error::Error;

use asset_importer::{Importer, postprocess::PostProcessSteps};
use clap::Parser;
use image::RgbaImage;
use pollster::block_on;

use crate::{cli::Args, renderer::Renderer};

pub mod cli;
pub mod renderer;
pub mod terminal;

fn main() -> Result<(), Box<dyn Error>> {
    let args = Args::parse();

    let scene = Importer::new().import_file_with(args.model_path.into_os_string(), |b| {
        b.with_post_process(
            PostProcessSteps::TRIANGULATE
                | PostProcessSteps::FLIP_UVS
                | PostProcessSteps::GEN_BOUNDING_BOXES,
        )
    })?;

    let renderer = block_on(Renderer::new(&scene.mesh(0).unwrap()))?;
    let pixels = RgbaImage::from_vec(64, 64, renderer.render()?).expect("Expected buffer");
    terminal::draw(pixels.into());

    Ok(())
}
