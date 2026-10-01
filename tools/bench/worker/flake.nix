{
  # views-shell-build-env — a native-NixOS FHS sandbox for building the
  # Chromium checkout FROM SOURCE on a NixOS bench host.
  #
  # Chromium's build pulls in a bundled toolchain (prebuilt clang, a Debian
  # sysroot, and depot_tools' cipd/vpython3/gn/ninja) — all FHS-assuming ELF
  # binaries that expect /lib64/ld-linux-x86-64.so.2 and a conventional
  # /usr layout that NixOS does not provide. `buildFHSEnv` gives them exactly
  # that layout inside a sandbox while staying Nix-reproducible.
  #
  # Usage on the bench:
  #   nix build path:.#builder -o ~/views-bench/fhs       # then ~/views-bench/fhs/bin/views-shell-build-env
  #   # then, inside:
  #   export PATH="$HOME/depot_tools:$PATH"               # (also set by profile)
  #   cd ~/chromium && gclient sync ... && cd src && ninja -C out/agency ...
  #
  # This flake reuses the system's own nixpkgs (flake:nixpkgs from the registry)
  # so it does not download a second nixpkgs tree on the worker.

  inputs.nixpkgs.url = "flake:nixpkgs"; # under systemd on NixOS pass --override-input nixpkgs path:<the registry path>

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true; # some fonts/codecs Chromium's deps pull
      };

      # Plain python3 for the few hooks that bypass vpython (mojom bindings,
      # GN codegen). vpython3 itself runs natively under the FHS loader and is
      # left enabled; these packages just make the fallback python usable too.
      pythonEnv = pkgs.python3.withPackages (ps: with ps; [
        ply jinja2 setuptools six requests
      ]);

      # The FHS package set. Chromium's Linux build is HERMETIC: with
      # use_sysroot=true (default) the compile/link uses a bundled Debian
      # sysroot downloaded during runhooks, NOT these host libs. So this list
      # only needs: (1) deps for the prebuilt depot_tools ELF tools
      # (cipd/vpython3/gn/ninja/clang) to RUN, and (2) runtime libs to LAUNCH
      # the produced `chrome` binary for a smoke test. Verified against the
      # nixpkgs chromium derivation's build/run inputs (NixOS 26.11).
      chromiumFhsPkgs = pkgs: (with pkgs; [
        # --- build drivers / depot_tools runtime ---
        pythonEnv
        git curl wget which
        gnumake gnutar gzip xz zstd unzip zip
        pkg-config
        cacert          # cipd/gsutil/curl TLS
        gnupg rsync patch procps file p7zip
        perl nasm
        ccache          # the CCACHE_DIR set in `profile` is inert without this

        # extra host build tools some hooks/gen steps shell out to
        nodejs_22 bison flex gperf

        # --- C/C++ runtime for the prebuilt ELF tools & produced binary ---
        stdenv.cc.cc.lib   # libstdc++ / libgcc_s
        glibc
        zlib openssl libffi libcap libunwind

        # --- X11 (launch-time) ---
        xorg.libX11 xorg.libxcb xorg.libXext xorg.libXrender
        xorg.libXrandr xorg.libXcomposite xorg.libXdamage xorg.libXfixes
        xorg.libXi xorg.libXtst xorg.libXcursor xorg.libXScrnSaver
        xorg.libxshmfence xorg.libXxf86vm

        # --- GL / graphics ---
        libGL libglvnd libGLU mesa libgbm libepoxy libdrm libva vulkan-loader

        # --- GTK / a11y / text ---
        gtk3 glib dbus-glib pango cairo
        atk at-spi2-core at-spi2-atk gdk-pixbuf gsettings-desktop-schemas
        fontconfig freetype harfbuzz dejavu_fonts noto-fonts

        # --- security (NSS/NSPR) ---
        nspr nss

        # --- system services (launch-time) ---
        dbus expat systemdLibs   # libudev
        libgcrypt cups alsa-lib libpulseaudio libnotify
        util-linux               # libuuid
        libkrb5                  # GSSAPI

        # --- Wayland (Ozone wayland platform + layer-shell) ---
        wayland wayland-protocols libxkbcommon
      ]);

      builder = pkgs.buildFHSEnv {
        name = "views-shell-build-env";
        targetPkgs = chromiumFhsPkgs;
        # Multi-arch not needed; Chromium x86_64 host build.
        runScript = "bash";
        profile = ''
          # depot_tools: never self-update (breaks reproducibility / FHS pins),
          # and put it on PATH ahead of everything.
          export DEPOT_TOOLS_UPDATE=0
          export DEPOT_TOOLS_METRICS=0
          export PATH="$HOME/depot_tools:$PATH"

          # ccache so every re-run after the first is incremental — the
          # "iterate as many times as we need" guarantee.
          export CCACHE_DIR="$HOME/.cache/ccache"
          export CCACHE_BASEDIR="$HOME/chromium/src"
          export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime
          # Full Chromium fills the default 5 GiB cache and thrashes; a fork
          # build's object set is ~40-60 GiB. Plenty of disk on the worker.
          export CCACHE_MAXSIZE=100G

          # TLS bundle for cipd/gsutil/curl inside the sandbox.
          export SSL_CERT_FILE=/etc/ssl/certs/ca-bundle.crt
          export GIT_SSL_CAINFO=/etc/ssl/certs/ca-bundle.crt

          echo "[views-shell-build-env] FHS sandbox ready."
          echo "  depot_tools: $HOME/depot_tools (PATH-first, UPDATE disabled)"
          echo "  ccache:      $CCACHE_DIR"
        '';
      };
    in {
      packages.${system} = {
        builder = builder;
        default = builder;
      };
      apps.${system}.default = {
        type = "app";
        program = "${builder}/bin/views-shell-build-env";
      };
      devShells.${system}.default = builder.env;
    };
}
