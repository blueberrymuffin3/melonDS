let
  pkgs = import <nixpkgs> {};
  devkitpro = pkgs.callPackage ./devkitpro.nix {};
in pkgs.mkShell {
  packages = [
    # Tools
    pkgs.ninja
    pkgs.cmake
    pkgs.clang
    pkgs.pkg-config
    devkitpro

    # Libs
    pkgs.curl
    pkgs.libpcap
    pkgs.SDL2
    pkgs.qt5Full
    pkgs.libslirp
    pkgs.glib
    pkgs.libarchive
    pkgs.libepoxy
  ];

  DEVKITPRO="${devkitpro}/opt/devkitpro";
  DEVKITA64="${devkitpro}/opt/devkitpro/devkitA64";

  # shellHook = ''
  #   export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
  # '';
}
