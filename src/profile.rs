// Optional Tracy profiling, built with `cargo build --release --features tracy`.
//
// Zones use the `profiling` crate's macros, which compile to nothing without
// the feature and, with it, also enable wgpu's own zones. Connect with a Tracy
// 0.13 viewer; data is only collected while it is connected.

// Start the Tracy client. Must run before any zone, which panics without a client.
pub fn start() {
    #[cfg(feature = "tracy")]
    tracy_client::Client::start();
}

// Record a value on a named Tracy plot
macro_rules! plot {
    ($name:literal, $value:expr) => {{
        #[cfg(feature = "tracy")]
        tracy_client::plot!($name, $value as f64);
        #[cfg(not(feature = "tracy"))]
        let _ = $value;
    }};
}
pub(crate) use plot;
