{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
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
}
