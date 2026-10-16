use std::process::ExitCode;

use clap::Parser;

use crate::{
    app::App,
    cli::{Args, CONTROLS, OutputMode},
};

pub mod app;
pub mod camera;
pub mod cli;
pub mod input;
pub mod model;
pub mod renderer;
pub mod terminal;

fn main() -> ExitCode {
    let args = Args::parse();

    if args.controls {
        print!("{CONTROLS}");
        return ExitCode::SUCCESS;
    }

    // Only half-block truecolor output exists so far
    if let Some(mode) = args.output_mode()
        && mode != OutputMode::TruecolorCharacters
    {
        eprintln!("dcat: {mode:?} output is not supported yet");
        return ExitCode::FAILURE;
    }

    // The terminal is restored when the app drops, so errors print on the normal screen
    let result = App::new(args).and_then(|mut app| app.run());
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("dcat: {error}");
            ExitCode::FAILURE
        }
    }
}
