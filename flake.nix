# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The development environment, pinned.
#
#   nix develop                                     a shell with the toolchain
#   nix develop -c ./prototype/hourly-overview/run.sh
#   nix develop -c cmake --preset dev               (once there is a build)
#
# This exists because "install the toolchain" is not a thing two machines can be
# asked to do and agree on. Qt 6, CMake, Ninja, clang-format, reuse and
# appstreamcli are five different package managers' worth of versions, and the
# ones that matter here - the Qt version the QML is written against, the
# clang-format that decides what the C++ looks like - are exactly the ones
# distros disagree about. A golden-image test cannot be reproducible on top of
# a toolchain that is not.
#
# The tool list and the shell hook are in nix/devshell.nix, shared with
# shell.nix so that flake and non-flake users get the same shell.
{
  description = "Climat - development shell for a native Qt 6 / QML weather app";

  # A release branch, not nixpkgs-unstable: unstable moves under you between
  # `nix develop` invocations on two machines on the same afternoon, and the
  # point of this file is that it does not.
  #
  # nixos-26.05 is the current stable channel and carries Qt 6.11.1, CMake 4.1.6
  # and Ninja 1.13.2. D2 sets the floor at Qt 6.8 LTS and asks that we develop
  # against 6.9+, and 6.11 is what the prototype has been running against all
  # along - run.sh picks the highest qtdeclarative in the store, which on this
  # machine is 6.11.1. Pinning to anything older would quietly change what the
  # prototype renders. Features newer than 6.8 stay behind version guards, which
  # is a code review question, not a toolchain one.
  #
  # flake.lock turns "the branch" into one revision. That file is the actual
  # reproducibility guarantee; this URL only says where the next `nix flake
  # update` should look.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      # Linux only, and deliberately. macOS and Windows are on the roadmap for
      # the app, not for this shell - appstream and desktop-file-utils are
      # Linux packaging tools, and a devShell nobody has ever entered is a
      # promise, not a feature. Add a system here when someone builds on it.
      systems = [ "x86_64-linux" "aarch64-linux" ];

      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = import ./nix/devshell.nix { inherit pkgs; };

        # fastlane, for scripts/play.sh to talk to Google Play. A shell of its
        # own because only a release needs it, and it is a Ruby world the
        # default shell has no other use for. The JDK is for jarsigner, which
        # checks the bundle is signed before Play is asked to take it.
        play = pkgs.mkShell {
          name = "climat-play";
          packages = with pkgs; [ fastlane python3 jdk21_headless ];
        };
      });

      # `nix fmt` on the Nix files. Nothing else in the tree is formatted by
      # nix - the QML is hand-aligned and qmlformat would flatten it.
      formatter = forAllSystems (pkgs: pkgs.nixpkgs-fmt);
    };
}
