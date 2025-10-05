{pkgs ? import <nixpkgs> {}}:
pkgs.mkShell {
  nativeBuildInputs = [pkgs.cmake pkgs.pkg-config];
  buildInputs = with pkgs; [
    gcc
    cmake
    libdrm
  ];

  shellHook = ''
    export CMAKE_PREFIX_PATH=${pkgs.libdrm}:$CMAKE_PREFIX_PATH
    echo "mkdir build && cd build && cmake .. && make"
  '';
}
