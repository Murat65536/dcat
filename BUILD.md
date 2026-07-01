# Building dcat

Instructions for building dcat from source. For prebuilt binaries, see [Install](README.md#install).

## Linux

Install the required dependencies:

```sh
# Add LunarG Vulkan SDK repository for latest vulkan-headers
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt-get update

sudo apt-get install -y gcc meson ninja-build libvulkan-dev vulkan-headers vulkan-utility-libraries-dev libassimp-dev libcglm-dev libsixel-dev pkg-config libvips-dev
```

Install the Slang shader compiler (`slangc` is not packaged in apt; download the release build and put it on `PATH`):

```sh
SLANG_VERSION=2026.11
curl -sSL -o slang.tar.gz "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz"
mkdir -p ~/.local/slang
tar -xzf slang.tar.gz -C ~/.local/slang
export PATH="$HOME/.local/slang/bin:$PATH"
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

## Windows

On Windows, the project is built using MSYS2. The clang64 environment is what CI builds and tests; other environments (ucrt64, mingw64) should work with the corresponding package prefix, but are untested.

```sh
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-meson mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-vulkan-headers mingw-w64-clang-x86_64-vulkan-loader mingw-w64-clang-x86_64-vulkan-utility-libraries mingw-w64-clang-x86_64-assimp mingw-w64-clang-x86_64-cglm mingw-w64-clang-x86_64-libvips git
```

You also need the Slang shader compiler (`slangc`) on `PATH`. MSYS2 does not package it; either install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (which bundles `slangc`) or extract a [Slang release](https://github.com/shader-slang/slang/releases) (`windows-x86_64`) and copy its `bin/` contents into `$MINGW_PREFIX/bin`.

When `bundled_libsixel` is enabled, the bundled libsixel is linked statically on Windows (while other dependencies remain DLL-packaged) to avoid patching upstream libsixel export macros.

Use `-Dbundled_libsixel=disabled` only if you intentionally want Meson to require a system `libsixel` package from the active MSYS2 environment.

### Windows Notes

Kitty SHM won't work on Windows

## Development

```sh
just setup-release # Configure a release build
just setup-debug   # configure a debug build
just build         # meson compile -C build
just asan          # debug build with AddressSanitizer + UBSan (Linux)
just ubsan         # debug build with UBSan only (Windows)
just devenv        # shell with the built dcat on PATH
just bump-wraps    # refresh subproject sources after editing subprojects/*.wrap
```
