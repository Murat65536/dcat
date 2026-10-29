# dcat

A 3d model viewer for the terminal

<img width="1092" height="614" alt="loop" src="https://github.com/user-attachments/assets/e0c3bb1e-a8f3-4cc6-bb7a-cca1505ab9c6" />


https://github.com/user-attachments/assets/ca47ea52-6a46-4ac2-8c9f-430fc9ea9865

## Install

Prebuilt binaries for every release are on the [releases page](https://github.com/Murat65536/dcat/releases).

### Linux and macOS

```sh
curl --proto '=https' --tlsv1.2 -LsSf https://github.com/Murat65536/dcat/releases/latest/download/dcat-installer.sh | sh
```

### Windows

```powershell
powershell -ExecutionPolicy Bypass -c "irm https://github.com/Murat65536/dcat/releases/latest/download/dcat-installer.ps1 | iex"
```

### Cargo

```sh
cargo install dcat
```

### From source

Install [Rust](https://rustup.rs/), then:

```sh
cargo build --release
./target/release/dcat model.glb
```

dcat needs a GPU with Vulkan drivers. On Linux, linking also needs zlib
(`zlib1g-dev` on Debian and Ubuntu).
