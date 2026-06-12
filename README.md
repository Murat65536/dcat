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

## Build

### Linux

Install the required dependencies:

```sh
# Add LunarG Vulkan SDK repository for latest vulkan-headers
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt-get update

sudo apt-get install -y gcc meson ninja-build libvulkan-dev vulkan-headers shaderc vulkan-utility-libraries-dev libassimp-dev libcglm-dev libsixel-dev pkg-config libvips-dev
```

Configure and build:

```sh
# Release
meson setup build --buildtype=release
meson compile -C build

# Debug
meson setup build-debug --buildtype=debug
meson compile -C build-debug
```

### Windows

On Windows, the project can be built using MSYS2 on clang64.

```sh
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-meson mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-vulkan-headers mingw-w64-clang-x86_64-vulkan-loader mingw-w64-clang-x86_64-shaderc mingw-w64-clang-x86_64-assimp mingw-w64-clang-x86_64-cglm mingw-w64-clang-x86_64-libvips git
```

Configure and build:

```sh
# Release (recommended for installer-bundled DLL distribution)
meson setup build --buildtype=release --default-library=shared -Dbundled_libsixel=enabled
meson compile -C build

# Debug
meson setup build-debug --buildtype=debug -Dbundled_libsixel=enabled
meson compile -C build-debug
```

When `bundled_libsixel` is enabled, the bundled libsixel is linked statically on Windows (while other dependencies remain DLL-packaged) to avoid patching upstream libsixel export macros.

Use `-Dbundled_libsixel=disabled` only if you intentionally want Meson to require a system `libsixel` package from the active MSYS2 environment.

### Windows Notes

Kitty SHM won't work on Windows

## Development

```sh
just setup-debug   # configure a debug build in ./build
just build         # meson compile -C build
just asan          # debug build with AddressSanitizer + UBSan (Linux; see note)
just ubsan         # debug build with UBSan only (use this on native Windows)
just devenv        # shell with the built dcat on PATH
just bump-wraps    # refresh subproject sources after editing subprojects/*.wrap
```
