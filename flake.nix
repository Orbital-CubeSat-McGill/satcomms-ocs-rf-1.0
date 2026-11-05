{
  description = "A very basic Zephyr flake";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/release-26.05";

    # Customize the version of Zephyr used by the flake here
    zephyr.url = "github:zephyrproject-rtos/zephyr/v4.4.0";
    zephyr.flake = false;

    zephyr-nix.url = "github:adisbladis/zephyr-nix";
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";
    zephyr-nix.inputs.zephyr.follows = "zephyr";
  };

  outputs = { self, nixpkgs, zephyr-nix, ... }: let
    pkgs = nixpkgs.legacyPackages.x86_64-linux;
    zephyr = zephyr-nix.packages.x86_64-linux;
  in {
    devShells.x86_64-linux.default = pkgs.mkShell {
	packages = [
          (zephyr.sdk.override { targets = [ "arm-zephyr-eabi" ]; })
          zephyr.pythonEnv
          zephyr.hosttools
          pkgs.cmake
          pkgs.ninja
          pkgs.hackrf
        ];
    };

    # Ground station, apart from the firmware shell so their Pythons can't clash
    devShells.x86_64-linux.ground = pkgs.mkShell {
      packages = [
        pkgs.gnuradio
        pkgs.gnuradio.pythonEnv
        pkgs.hackrf
      ];
      SOAPY_SDR_PLUGIN_PATH = "${pkgs.soapyhackrf}/lib/SoapySDR/modules0.8-3";
      QT_PLUGIN_PATH = "${pkgs.gnuradio.qt.qtbase.bin}/${pkgs.gnuradio.qt.qtbase.qtPluginPrefix}";
    };
  };
}
