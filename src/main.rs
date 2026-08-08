use std::error::Error;

use asset_importer::{Importer, postprocess::PostProcessSteps};
use clap::Parser;
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

    let size = ratatui::crossterm::terminal::size()?;
    let size = (size.0 as u32, (size.1 * 2) as u32);

    let renderer = block_on(Renderer::new(&scene.mesh(0).unwrap(), size.0, size.1))?;
    let pixels = renderer.render()?;
    terminal::draw(pixels, size.0, size.1);

    Ok(())
}
