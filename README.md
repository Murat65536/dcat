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

Linux/macOS:

```sh
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release -D USE_BUNDLED=OFF -D USE_SANITIZERS=OFF
cmake --build build --config Release
```

Debug build with sanitizers:

```sh
cmake -S . -B build-debug -G Ninja -D CMAKE_BUILD_TYPE=Debug -D USE_BUNDLED=OFF -D USE_SANITIZERS=ON
cmake --build build-debug --config Debug
```

Windows (MSVC, recommended):

```powershell
# Run these in a Visual Studio Developer PowerShell (x64)
cmake -S cmake.deps -B .deps -G Ninja -D CMAKE_BUILD_TYPE=Release -D DCAT_VCPKG_TRIPLET=x64-windows
cmake --build .deps --config Release
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release -D USE_BUNDLED=ON -D DEPS_PREFIX="$PWD\.deps\vcpkg_installed\x64-windows" -D USE_SANITIZERS=OFF
cmake --build build --config Release
```

### Build bundled dependencies only (Windows/MSVC)

```powershell
cmake -S cmake.deps -B .deps -G Ninja -D CMAKE_BUILD_TYPE=Release -D DCAT_VCPKG_TRIPLET=x64-windows
cmake --build .deps --config Release
```

If your vcpkg revision does not provide a `vips`/`libvips` port, `cmake.deps` automatically downloads the official
`vips-dev-x64-all` bundle from `libvips/build-win64-mxe` and places it under `.deps\vcpkg_installed\x64-windows`.

Then configure the main project with:

```powershell
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release -D USE_BUNDLED=ON -D DEPS_PREFIX="$PWD\.deps\vcpkg_installed\x64-windows"
cmake --build build --config Release
```

Windows notes:
- `--kitty`, `--kitty-direct`, and `--sixel` are not supported in native Windows mode.
- Auto output mode falls back to character rendering modes on Windows.
- `glslc` must still be available on `PATH` (for example from the Vulkan SDK).

Install:

```sh
cmake --install build
```
