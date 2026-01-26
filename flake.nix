{
  description = "wlr-hdr-calibrator";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    wlr-protocols = {
      url = "github:swaywm/wlr-protocols";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, wlr-protocols }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = {
        wlr-hdr-calibrator = pkgs.stdenv.mkDerivation {
          pname = "wlr-hdr-calibrator";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
            glib # for glib-compile-resources
            git
          ];

          buildInputs = with pkgs; [
            gtk3
            wayland
            wayland-protocols
          ];


          # Fix for sandbox: link wlr-protocols and disable git submodule commands
          # The patching happens only in the build sandbox, not on your disk.
          postPatch = ''
            rm -rf wlr-protocols
            ln -s ${wlr-protocols} wlr-protocols
            
            # Remove git submodule commands block
            # Matches from 'git = find_program' up to '# Run wayland-scanner'
            # and deletes everything except the last line.
            sed -i "/git = find_program/,/# Run wayland-scanner/ {
              /# Run wayland-scanner/!d
            }" meson.build
          '';
        };
        default = self.packages.${system}.wlr-hdr-calibrator;
      };
      
      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
          git
          glib
          wayland
          wayland-scanner
        ];
        buildInputs = with pkgs; [
          gtk3
          wayland
          wayland-protocols
        ];
      };
    };
}
