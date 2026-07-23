# dcat

A 3d model viewer for the terminal

<img width="1092" height="614" alt="loop" src="https://github.com/user-attachments/assets/e0c3bb1e-a8f3-4cc6-bb7a-cca1505ab9c6" />


https://github.com/user-attachments/assets/ca47ea52-6a46-4ac2-8c9f-430fc9ea9865

## Install

Prebuilt binaries for every release are on the [releases page](https://github.com/Murat65536/dcat/releases).

See [BUILD.md](BUILD.md) to build from source.


### Windows

Download and run `dcat-windows-setup.exe`.

### Linux

Download and extract the tarball; the binary finds its `shaders/` directory next to itself, so it runs in place:

```sh
tar -xzf dcat-linux-x86_64.tar.gz
./dcat-linux/dcat
```


The binary is dynamically linked, so the runtime libraries (Vulkan, assimp, Chafa, libvips) must be installed from your package manager.

## Terminal output

With no output-mode flag, dcat uses Chafa's terminal database to select Kitty, Sixel, iTerm2, truecolor, indexed color, or monochrome output. The existing output flags still force a mode; `--kitty` keeps the local shared-memory fast path, while `--kitty-direct` uses Chafa's inline Kitty encoder.
