# Dev shell for STANDSTILL:  nix-shell  then  make run
{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
  name = "standstill";
  nativeBuildInputs = [ pkgs.gcc pkgs.gnumake pkgs.pkg-config ];
  buildInputs = [ pkgs.SDL2 ];
}
