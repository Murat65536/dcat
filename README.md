# dcat

A 3d model viewer for the terminal

https://github.com/user-attachments/assets/ca47ea52-6a46-4ac2-8c9f-430fc9ea9865

## Install

Prebuilt binaries for every release — plus a `nightly` prerelease — are on the [releases page](https://github.com/Murat65536/dcat/releases). Alternatively, build from source (see [BUILD.md](BUILD.md)).

### Windows

Download and run `dcat-windows-setup.exe`.

### Linux

Download and extract the tarball; the binary finds its `shaders/` directory next to itself, so it runs in place:

```sh
tar -xzf dcat-linux-x86_64.tar.gz
./dcat-linux/dcat
```

The binary is dynamically linked, so the runtime libraries (Vulkan, assimp, libsixel, libvips) must be installed from your package manager. It is built on Ubuntu 24.04; on distros with incompatible library versions, build from source instead.

## Build

To build from source, see [BUILD.md](BUILD.md).

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&theme=dark&legend=top-left" />
  <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&legend=top-left" />
  <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Murat65536/dcat&type=date&legend=top-left" />
</picture>
