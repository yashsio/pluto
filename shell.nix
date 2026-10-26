{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    gcc
    gnumake
    pkg-config
    wayland-scanner
    wayland
    libxkbcommon
  ];
}
