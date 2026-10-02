# build

The build configuration the bench uses. The lifted wire scripts, link gate and
development `args.gn` that once sat here were deleted in chapter 2 (w1e); their
jobs are done by `tools/bench/` and by the two args files below.

- `CHROMIUM_VERSION`: the pinned tag, **154.0.8037.92** (hops are chosen by hand,
  [`../../docs/chromium-hop.md`](../../docs/chromium-hop.md)).
- `args.release.gn`: the release, non-component configuration, the source of
  record for every footprint measurement. `is_official_build = false` because
  the PGO profiles are not checked out on the bench; `symbol_level = 0`;
  `dcheck_always_on = false` set explicitly. `tools/bench/args.release.gn` is
  this file plus the bench lines (`use_remoteexec = false`, `use_siso = false`,
  `cc_wrapper = "ccache"`, `enable_ax_automation_bindings = false` for finding
  F1); keep the two in step. The development (component) configuration is
  `tools/bench/args.views.gn` alone.
- The executable is built with `assert_no_deps` on `//chrome`, `//ash`,
  `//chromeos`, `//content`, `//gin`, `//third_party/blink/renderer` and `//v8`
  (`../BUILD.gn`), and `gn path` to each of `//v8`, `//content`, both Blink
  renderer trees and `//chrome` prints `No non-data paths` in both build
  directories. `//ash` is not a label at all in an `is_linux` build.

Hops are run by hand on the bench, within the compile budget Tom sets. There is
no timer and no CI job.

## The two build directories

`out/views` is the component build everyone develops in; `out/release` is the
non-component build for footprint. Both are brought to their args file by
`tools/bench/worker/ensure-out.sh <out> <args-file>` (idempotent: copies the
args when they differ or when siso state is present, `gn clean` in that case,
`gn gen`, `ENSURE-OK <out> cleaned|unchanged`). The bench is shared, so
`out/views` holds whatever the last lock holder built: a prove that runs a
binary runs inside the lock hold that built it.

## Measuring footprint

The procedure, as run for chapter 2 (records:
[`../../docs/bench/views-shell-154-release.md`](../../docs/bench/views-shell-154-release.md)
and `views-shell-154-assembled.md`; the committed sequences are
`tools/bench/seq/w1b.sh` and `seq/w3c.sh` stage `release`). Everything runs on
the bench, under `~/views-bench/bench.lock`, as a transient user unit:

1. Wire: `tools/bench/worker/wire.sh <worktree>` resets the checkout, copies
   `shell/` to `src/views_shell/`, mirrors `style/`, `schemas/`, `examples/`
   and `tools/fixtures/` to `src/views_shell/data/{style,schemas,examples,fixtures}`,
   applies `shell/patches/series` with plain `git apply` and prints `WIRE-OK`.
2. Configure: inside the FHS environment,
   `tools/bench/worker/ensure-out.sh release tools/bench/args.release.gn`.
3. Build: `autoninja -C out/release views_shell views_examples`.
4. Census: `tools/bench/worker/census.sh <out> <label> <binary> <report>` for
   each binary: size as linked and after the checkout's `llvm-strip`
   (`third_party/llvm-build/Release+Asserts/bin/llvm-strip`, not on PATH),
   `ldd` line count, `gn desc ... runtime_deps` file count and bytes, and
   `gn desc ... deps --all` grouped by the first and the first two path
   segments. Then the five `gn path` checks must each print `No non-data paths`.
5. Footprint: `tools/bench/worker/headless.sh <results> ~/views-bench/build-env -c
   '<binary> --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar [--theme <dir>]'`
   under the same harness: `runtime-test`, headless stock scroll with pixman,
   scratch `HOME` and XDG directories, the system bus pointed at a dead address
   (rule R19). `measure.sh` writes one row (PSS from `smaps_rollup`, RSS, idle
   CPU, ctxsw/s, threads, FDs) after the client settles. A GPU-fair row uses
   `runtime-test-gpu --allow-dri` with a gles2 compositor on `renderD128`
   (`tools/bench/README.md`, "headless.sh switches").

Stock `views_examples` attaches no buffer on this harness (finding F2), so its
row is a cost control, not a drawing control. The numbers at the close of
chapter 2 are in [`../../docs/bench/README.md`](../../docs/bench/README.md).
