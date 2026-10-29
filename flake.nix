{
  description = "dcat - 3D terminal model renderer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: import nixpkgs { inherit system; };
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = pkgsFor system;
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              rustup
              pkg-config
              cmake
              gnumake
              clang
              vulkan-tools
              # Statically linked Assimp needs zlib at link time
              zlib
            ];

            # wgpu and native dependencies (such as asset-importer) require these at runtime.
            LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath (with pkgs; [
              vulkan-loader
              zlib
              libGL
              wayland
              libxkbcommon
              libx11
              libxcursor
              libxi
              libxrandr
            ]);
          };
        });
    };
}
