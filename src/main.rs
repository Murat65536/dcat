use std::process::ExitCode;

use clap::Parser;

use crate::{
    app::App,
    cli::{Args, CONTROLS},
};

pub mod app;
pub mod camera;
pub mod cli;
pub mod gpu_model;
pub mod input;
pub mod model;
pub mod output;
pub mod profile;
pub mod renderer;
pub mod terminal;
pub mod texture;

fn main() -> ExitCode {
    let args = Args::parse();
    profile::start();
    profiling::register_thread!();

    if args.controls {
        print!("{CONTROLS}");
        return ExitCode::SUCCESS;
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
