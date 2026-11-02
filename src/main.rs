use std::process::ExitCode;

use clap::Parser;

use crate::{
    app::App,
    cli::{Args, CONTROLS},
};

pub mod animation;
pub mod app;
pub mod camera;
pub mod cli;
pub mod gpu_model;
pub mod input;
pub mod model;
pub mod profile;
pub mod renderer;
pub mod texture;

fn main() -> ExitCode {
    let args = Args::parse();
    profile::start();
    profiling::register_thread!();

    if args.controls {
        print!("{CONTROLS}");
        return ExitCode::SUCCESS;
    }
    if args.detect_output {
        println!("{}", args.chosen_output_mode().name());
        return ExitCode::SUCCESS;
    }

    // The terminal is restored when the app drops, so errors print on the normal screen
    let result = if let Some(path) = &args.image {
        app::save_image(&args, path)
    } else if args.once {
        app::print_once(&args)
    } else {
        App::new(args).and_then(|mut app| app.run())
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("dcat: {error}");
            ExitCode::FAILURE
        }
    }
}
