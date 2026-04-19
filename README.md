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

Linux/macOS:

```sh
conan profile detect --force
conan install . --output-folder=build --build=missing -s build_type=Release
meson setup build --native-file build/conan_meson_native.ini --buildtype=release -Duse_sanitizers=false
meson compile -C build
```

Debug build with sanitizers:

```sh
conan install . --output-folder=build-debug --build=missing -s build_type=Debug
meson setup build-debug --native-file build-debug/conan_meson_native.ini --buildtype=debug -Duse_sanitizers=true
meson compile -C build-debug
```

Windows:

```powershell
py -m pip install --user --upgrade conan
conan profile detect --force
conan install . --output-folder=build --build=missing -s build_type=Release
meson setup build --native-file build\conan_meson_native.ini --buildtype=release -Duse_sanitizers=false
meson compile -C build
```

If your PATH is still broken/truncated on Windows, create a shim once:

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.local\bin" | Out-Null
Set-Content "$env:USERPROFILE\.local\bin\conan.cmd" @(
  '@echo off',
  '"%APPDATA%\Python\Python314\Scripts\conan.exe" %*'
) -Encoding Ascii
```

Then make sure `C:\Users\<you>\.local\bin` is on your `PATH`.

Windows notes:
- `--kitty`, `--kitty-direct`, and `--sixel` are not supported in native Windows mode.
- Auto output mode falls back to character rendering modes on Windows.
- `glslc` must still be available on `PATH` (for example from the Vulkan SDK).

CI build coverage:
- `build` workflow validates Linux (`release` + `debug`) and Windows (`release`) builds.
- Windows CI uses an MSYS2 `MINGW64` toolchain with Vulkan, shaderc/glslc, assimp, cglm, and libvips packages.
- `Release` workflow continues to publish the Linux tarball (`dcat-linux-x86_64.tar.gz`); the Windows job is build validation only.

Install:

```sh
meson install -C build
```
