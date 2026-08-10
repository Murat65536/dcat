use std::{error::Error, time::Duration};

use asset_importer::{Importer, postprocess::PostProcessSteps};
use clap::Parser;
use crossterm::event::{self, Event, KeyEventKind};
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

    let size = crossterm::terminal::size()?;
    let size = (size.0 as u32, (size.1 * 2) as u32);

    let renderer = block_on(Renderer::new(&scene.mesh(0).unwrap(), size.0, size.1))?;
    let mut term = terminal::Terminal::new()?;

    loop {
        let screen = renderer.render()?;
        term.draw(screen, size.0, size.1)?;
        if event::poll(Duration::from_millis(10))?
            && let Event::Key(key) = event::read()?
            && key.kind == KeyEventKind::Press
        {
            break;
        }
    }
    Ok(())
}
