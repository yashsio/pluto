{ pkgs ? import <nixpkgs> { } }:

pkgs.stdenv.mkDerivation {
  pname = "pluto";
  version = "0.3";
  src = ./.;

  nativeBuildInputs = with pkgs; [
    pkg-config
    wayland-scanner
  ];
  buildInputs = with pkgs; [
    wayland
    libxkbcommon
  ];

  buildPhase = ''
    runHook preBuild
    # The source tree may contain a stale ./pluto from a previous nix-shell
    # build.  Remove it so make relinks with this derivation's libraries, and
    # keep the user's config.h instead of letting make copy config.def.h over
    # it (make -B would force that copy).
    [ -e config.h ] || cp config.def.h config.h
    touch config.h
    rm -f pluto
    make
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp pluto $out/bin/pluto
    runHook postInstall
  '';
}
