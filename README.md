# dcat

A 3d model viewer for the terminal

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


The binary is dynamically linked, so the runtime libraries (Vulkan, assimp, libsixel, libvips) must be installed from your package manager.

## Star History

 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&legend=top-left" />
 </picture>
