# Building

## Requirements

- [Rust](https://rustup.rs/)
- A GPU with Vulkan drivers
- On Linux, zlib (`zlib1g-dev` on Debian and Ubuntu)

## Build

```sh
cargo build --release
./target/release/dcat model.glb
```

## Older Linux distributions

On Linux with glibc older than 2.38, the prebuilt Assimp library fails to link. Build it from source instead. This needs CMake:

```sh
ASSET_IMPORTER_FORCE_BUILD=1 cargo build --release
```

The same applies to `cargo install dcat`.
