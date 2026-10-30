# dcat

A 3d model viewer for the terminal

<img width="1092" height="614" alt="loop" src="https://github.com/user-attachments/assets/e0c3bb1e-a8f3-4cc6-bb7a-cca1505ab9c6" />


https://github.com/user-attachments/assets/ca47ea52-6a46-4ac2-8c9f-430fc9ea9865

## Install

Linux and macOS:

```sh
curl --proto '=https' --tlsv1.2 -LsSf https://github.com/Murat65536/dcat/releases/latest/download/dcat-installer.sh | sh
```

Windows:

```powershell
powershell -ExecutionPolicy Bypass -c "irm https://github.com/Murat65536/dcat/releases/latest/download/dcat-installer.ps1 | iex"
```

Or with Cargo: `cargo install dcat`. To build from source, see [BUILD.md](BUILD.md).

## Usage

```sh
dcat model.glb
```

Run `dcat --help` for options and `dcat --controls` for the keys.
