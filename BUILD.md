# Building

## Quick Start

1. Install the [dependencies](#dependencies) for your platform and the [Slang compiler](#slang-shader-compiler).
2. Configure a build: `just setup-release`
3. Compile it: `just build`
4. Run the result: `build/dcat`

Sanitizer builds: `just asan` (Linux) or `just ubsan` (Windows). Test coverage: `just coverage` (requires [gcovr](https://gcovr.com/) >= 8)

## Dependencies

### Linux

Install the build toolchain and libraries:

```sh
# Add LunarG Vulkan SDK repository for latest vulkan-headers
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt-get update

sudo apt-get install -y gcc meson ninja-build libvulkan-dev vulkan-headers vulkan-utility-libraries-dev libassimp-dev libcglm-dev libchafa-dev pkg-config libvips-dev
```

Chafa 1.16 or newer is required. If your distribution ships an older release, install a current Chafa release before configuring dcat.

### Windows (MSYS2)

On Windows the project is built with [MSYS2](https://www.msys2.org/). The **clang64** environment is what CI builds and tests.

Other environments (ucrt64, mingw64) should work with the corresponding package prefix, but are untested.

```sh
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-meson mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-vulkan-headers mingw-w64-clang-x86_64-vulkan-loader mingw-w64-clang-x86_64-vulkan-utility-libraries mingw-w64-clang-x86_64-assimp mingw-w64-clang-x86_64-cglm mingw-w64-clang-x86_64-chafa mingw-w64-clang-x86_64-libvips mingw-w64-clang-x86_64-just git
```

### Slang shader compiler

Both platforms need the Slang shader compiler. It is not packaged by apt or MSYS2, so install it separately.

**Linux** — download a release build and add it to `PATH`:

```sh
SLANG_VERSION=2026.11
curl -sSL -o slang.tar.gz "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz"
mkdir -p ~/.local/slang
tar -xzf slang.tar.gz -C ~/.local/slang
export PATH="$HOME/.local/slang/bin:$PATH"
```

**Windows** — either install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (which bundles `slangc`), or extract a [Slang release](https://github.com/shader-slang/slang/releases) (`windows-x86_64`) and copy its `bin/` contents into `$MINGW_PREFIX/bin`.
