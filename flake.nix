{
  description = "WebAssembly bytecode interpreter";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        ghcWithShake = pkgs.haskellPackages.ghcWithPackages (ps: [ ps.shake ]);
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "wasm-vm";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.cmake ];
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp wasm-vm $out/bin/
            runHook postInstall
          '';
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            gnumake
            clang
            clang-tools
            ccls
            wabt
            wasmtime
            ghcWithShake
          ];
          shellHook = ''
            export WASM_CC=${pkgs.llvmPackages.clang-unwrapped}/bin/clang
            echo "wasm-interpreter dev shell"
            echo "  cmake build:  make build"
            echo "  shake build:  runghc Shakefile.hs"
            echo "  shake tests:  runghc Shakefile.hs tests"
            echo "  run grader:   runghc Shakefile.hs test"
          '';
        };
      });
}
