# dcat

A 3d model viewer for the terminal

https://github.com/user-attachments/assets/ca47ea52-6a46-4ac2-8c9f-430fc9ea9865

## Controls

- `q` - Quit
- `m` - Toggle wireframe
- Animation controls:
  - `p` - Play/Pause animation
  - `1` - Previous animation
  - `2` - Next animation
- When FPS controls are enabled:
  - `w/a/s/d` - Move forward/left/backward/right
  - `i/j/k/l` - Look around
  - `Space` - Move up
  - `Shift` - Move down
  - `v` - Slow down
  - `b` - Speed up
- When FPS controls are disabled
  - `w/a/s/d` - Rotate the camera around the model
  - `e/r` - Zoom in and out

## Character rendering option

- `--hash-characters` makes character modes (`--truecolor-characters`, `--palette-characters`, `--block-characters`) render with `#` instead of half-block glyphs.
- In this mode, vertical detail is halved (one source pixel row per terminal row).

## Build

The project now uses repo-local `vcpkg` manifest mode by default.

### Bootstrap `vcpkg`

The presets expect a `vcpkg` checkout at `.deps/vcpkg`:

```powershell
git clone https://github.com/microsoft/vcpkg.git .deps/vcpkg
.deps\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

### Configure and build

Release:

```sh
cmake --preset release
cmake --build --preset release
```

Debug:

```sh
cmake --preset debug
cmake --build --preset debug
```

The manifest is pinned via `vcpkg.json` and uses local overlay ports under `vcpkg-overlays/ports` for awkward packages:
- `libsixel` is built from a pinned upstream commit and exported as `unofficial-libsixel`
- `vips` uses the official Windows prebuilt bundle and is exported as `unofficial-vips`

Current note on `vips`:
- Windows is managed directly through the overlay port
- Linux/macOS still rely on system `pkg-config`/package-manager installs for `vips`

Legacy path:
- `cmake.deps` and `-DUSE_BUNDLED=ON` still work as a fallback for the old `.deps/vcpkg_installed/...` prefix flow

Windows notes:
- `--kitty` and `--kitty-direct` are not supported in native Windows mode
- Auto output mode falls back to character rendering modes on Windows
- `glslc` must still be available on `PATH` or provided by the resolved dependency prefix

Install:

```sh
cmake --install build/release
```
