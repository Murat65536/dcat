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

This project uses the [Meson](https://mesonbuild.com/) build system and depends on Vulkan, Assimp, cglm, libvips, and libsixel.

### Linux

Install the required dependencies (Ubuntu/Debian example):

```sh
# Add LunarG Vulkan SDK repository for latest vulkan-headers
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt-get update

sudo apt-get install -y gcc meson ninja-build libvulkan-dev vulkan-headers shaderc vulkan-utility-libraries-dev libassimp-dev libcglm-dev libsixel-dev pkg-config libvips-dev python3-pytest
```

Configure and build:

```sh
# Release
meson setup build --buildtype=release
meson compile -C build
meson test -C build

# Debug
meson setup build-debug --buildtype=debug
meson compile -C build-debug
meson test -C build-debug
```

### Windows

On Windows, the project is built using [MSYS2](https://www.msys2.org/) with the `clang64` environment. The Windows build uses the bundled libsixel subproject by default to avoid cross-environment `pkg-config` issues.

1. Install MSYS2.
2. Open the **MSYS2 Clang x86_64** terminal.
3. Install dependencies:

```sh
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-meson mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-vulkan-headers mingw-w64-clang-x86_64-vulkan-loader mingw-w64-clang-x86_64-shaderc mingw-w64-clang-x86_64-assimp mingw-w64-clang-x86_64-cglm mingw-w64-clang-x86_64-libvips mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-python-pytest git
```

Configure and build:

```sh
# Release (recommended for standalone usage)
meson setup build --buildtype=release --default-library=static -Dbundled_libsixel=enabled
meson compile -C build
meson test -C build

# Debug
meson setup build-debug --buildtype=debug -Dbundled_libsixel=enabled
meson compile -C build-debug
meson test -C build-debug
```

Use `-Dbundled_libsixel=disabled` only if you intentionally want Meson to require a system `libsixel` package from the active MSYS2 environment.

### Windows Notes
- `--kitty` and `--kitty-direct` are not supported in native Windows mode.
- Auto output mode falls back to character rendering modes on Windows.
