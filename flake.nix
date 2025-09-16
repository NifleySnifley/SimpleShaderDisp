{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        # canup = pkgs.writeShellScript
      in
      with builtins;
      rec {

        devShells.default = pkgs.mkShell {
          name = "ssdisp";
          packages =
            with pkgs; [
              bear
              gcc
              gdb
			  sfml_2
			  gcc15
			  xorg.libX11.dev
            ];
        };
      }
    );
}