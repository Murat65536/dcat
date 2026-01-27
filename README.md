# dcat

A terminal-based 3D model viewer, written in C++ with Vulkan.

## Dependencies

- CMake 3.16+
- Vulkan SDK (with glslc shader compiler)
- Assimp (for model loading)
- GLM (for math)

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install cmake libvulkan-dev vulkan-tools glslc libassimp-dev libglm-dev
```

**Fedora:**
```bash
sudo dnf install cmake vulkan-devel vulkan-tools glslc assimp-devel glm-devel
```

**Arch Linux:**
```bash
sudo pacman -S cmake vulkan-devel glslc assimp glm
```

## Building

```bash
cd cpp
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## Usage

```bash
./dcat [OPTIONS] MODEL
```

## Examples

```bash
# View a model with default settings
./dcat model.obj

# View with a texture
./dcat -t texture.png model.obj

# Use Kitty graphics protocol for better quality
./dcat -K model.obj

# Use Sixel graphics (for terminals that support it)
./dcat -S model.obj
```

## Controls

- `q` - Quit the viewer

When `--fps-controls` is enabled:
- `W/A/S/D` - Move forward/left/backward/right
- `I/J/K/L` - Look around
- Space - Move up
- Shift - Move down

## License

MIT
