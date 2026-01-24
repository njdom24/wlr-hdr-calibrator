{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "hdr-lut-editor-shell";

  packages = with pkgs; [
    python3
    python3Packages.pyqt6
  ];
}
