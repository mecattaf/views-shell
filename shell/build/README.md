# build

Lifted from agency-mvp `cd7cef3` (see `../PROVENANCE.md`). These files still say
`agency`; the rename is part of the re-cut.

- `CHROMIUM_VERSION`: the last green tag in this lineage, **150.0.7871.124**.
  views-shell's first hop re-cuts at a current tag, chosen by hand
  ([`../../docs/chromium-hop.md`](../../docs/chromium-hop.md)).
- `args.gn`: the lifted development configuration (component build). Planned
  changes before first use: drop `agency_enable_inbound_server`; replace
  `enable_agency` with `enable_views_shell`; **set `dcheck_always_on = false`
  explicitly**. The lineage set it because the renderer and Blink aborted with
  DCHECKs on at 148 and 149; views-shell has no renderer (rule R24), so the reason is
  gone, but the argument stays explicit until a content-free build shows it can
  be dropped.
- `args.release.gn`: the release, non-component configuration, the source of
  record for every footprint measurement (no longer a sketch).
  `is_official_build = false` because the PGO profiles are not checked out on
  the bench; `symbol_level = 0`. `tools/bench/args.release.gn` is this file
  plus the bench lines (`use_remoteexec = false`, `use_siso = false`,
  `cc_wrapper = "ccache"`, `enable_ax_automation_bindings = false` for finding
  F1); keep the two in step. `views_examples` (no `//content`) is built beside
  `//views_shell:views_shell` in this configuration as the control.
- `wire-agency.sh`, `wire-and-gen.sh`, `verify-compilation.sh`, `LINK-GATE.md`:
  the wire and link gates. Planned: drop the host-overlay weld and the inbound
  server; never fall back to `git apply --3way`; build the Views program with
  `assert_no_deps` on `//chrome`, `//ash`, `//chromeos`, `//content`,
  `//third_party/blink/renderer` and `//v8`.

Hops are run by hand on the build host, within the compile budget Tom sets. There
is no timer and no CI job.

## Measuring footprint

The procedure, as run for chapter 2 item w1b (record:
[`../../docs/bench/views-shell-154-release.md`](../../docs/bench/views-shell-154-release.md);
the committed sequence is `tools/bench/seq/w1b.sh`). Everything runs on the
bench, under `~/views-bench/bench.lock`, as a transient user unit:

1. Wire: `tools/bench/worker/wire.sh <worktree>` resets the checkout, copies
   `shell/` to `src/views_shell/`, mirrors `style/`, `schemas/`, `examples/`
   and `tools/fixtures/` to `src/views_shell/data/{style,schemas,examples,fixtures}`,
   applies `shell/patches/series` with plain `git apply` and prints `WIRE-OK`.
2. Configure: inside the FHS environment,
   `tools/bench/worker/ensure-out.sh release tools/bench/args.release.gn`.
   It copies the args file over `out/release/args.gn` when they differ or when
   siso state (`.siso_deps`) is present, runs `gn clean` in that case (siso back
   to ninja needs it), runs `gn gen` and prints `ENSURE-OK release`. The same
   script restores `out/views` from `tools/bench/args.views.gn`.
3. Build: `autoninja -C out/release views_shell views_examples`.
4. Census: `tools/bench/worker/census.sh <out> <label> <binary> <report>` for
   each binary: size as linked and after the checkout's `llvm-strip`
   (`third_party/llvm-build/Release+Asserts/bin/llvm-strip`, not on PATH),
   `ldd` line count, `gn desc ... runtime_deps` file count and bytes, and
   `gn desc ... deps --all` grouped by the first and the first two path
   segments. Then `gn path out/release //views_shell:views_shell <t>` for
   `//v8`, `//content`, `//third_party/blink/renderer/core`,
   `//third_party/blink/renderer/platform` and `//chrome` must each print
   `No non-data paths`.
5. Footprint: `tools/bench/worker/headless.sh <results> ~/views-bench/build-env -c
   '<binary> --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader [--bar]'`
   for both binaries, under the same harness: `runtime-test`, headless stock
   scroll with pixman, scratch `HOME` and XDG directories, the system bus
   pointed at a dead address (rule R19). `measure.sh` writes one row (PSS from
   `smaps_rollup`, RSS, idle CPU, ctxsw/s, threads, FDs) after the client
   settles.

A GPU-fair run needs `/dev/dri` inside the sandbox, which `runtime-test` does
not expose (finding F2): every row is SwiftShader into `wl_shm` buffers on a
pixman compositor. Stock `views_examples` attaches no buffer on this harness
(F2), so its row is a non-drawing control.
