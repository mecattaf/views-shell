# `views_examples` at 154.0.8037.92 on the bench

The content-free baseline of this chapter: stock Chromium `views_examples`
(`//ui/views/examples:views_examples`), built in `out/views` on the bench and
run as a client of headless stock scroll under `runtime-test`. This page
records the build configuration, the run, the measured numbers and the two
findings (F1, F2) it produced. `SPEC.md` carries the full finding texts; this
is the bench record.

## What ran

- Checkout: `~/chromium/src` at tag `154.0.8037.92` (the newest Chrome stable
  at bench time, 2026-10-01; see `docs/chromium-hop.md` for the policy).
- Binary: `~/chromium/src/out/views/views_examples`, built by the bootstrap
  unit (mark `~/views-bench/marks/views-examples-build`) and rebuilt stock by
  unit `t2-build` after the F2 experiments below had been reverted from the
  sources.
- Build environment: `~/views-bench/build-env`, built from
  `tools/bench/worker/flake.nix` (the FHS sandbox, with fonts).
- Harness: `tools/bench/worker/headless.sh` under `runtime-test` — headless
  stock scroll (`WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`, one 1920x1080
  output), client with `WAYLAND_DEBUG=1`, then `scrollmsg -t get_tree`, a grim
  screenshot, a colour sample and a `measure.sh` row.
- Client command:

  ```text
  ~/views-bench/build-env -c "~/chromium/src/out/views/views_examples \
    --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
  ```

  The two GL switches are mandatory on the bench (finding F2): inside
  `runtime-test` there is no `/dev/dri`, and the default GL path dies silently
  after the first configure; `--use-gl=angle --use-angle=swiftshader` keeps
  the client alive. A real seat with a render node needs neither.

## Build args of `out/views`

`~/chromium/src/out/views/args.gn` (the copy of record is
`tools/bench/args.views.gn`):

```gn
is_component_build = true
is_debug = false
symbol_level = 1
dcheck_always_on = false
target_os = "linux"
use_ozone = true
ozone_auto_platforms = false
ozone_platform_wayland = true
ozone_platform = "wayland"
use_remoteexec = false
use_siso = false
cc_wrapper = "ccache"
enable_ax_automation_bindings = false
```

The last line is the point of finding **F1**: at 154.0.8037.92 the stock tree
gives `//ui/views` → `//ui/accessibility/platform` → `//gin` → `//v8` through
the Automation API bindings, and `enable_extensions = false` is not an option
(`ui/webui/webui_features.gni` asserts `enable_extensions_core`).
`shell/patches/views-shell-ax-automation-bindings.patch` (7 lines, in
`shell/patches/series`, applied by `worker/wire.sh` with plain `git apply` per
rule R20) puts that one block of `ui/accessibility/platform/BUILD.gn` behind a
new arg `enable_ax_automation_bindings`, default unchanged, and the bench sets
it false. That keeps `libv8.so` (194 MB in the component build) out of a
content-free Views program. With the patch applied and the arg false, `gn path
out/views //ui/views/examples:views_examples` finds no non-data path to any of
`//v8`, `//content`, `//third_party/blink/renderer/core` or
`//third_party/blink/renderer/platform` — four times "No non-data paths found
between these two targets."

## The run

`summary.txt` of the recorded run (`~/views-bench/results/t2-examples`):

```text
compositor: wayland-1 /run/user/1000/scroll-ipc.1000.9.sock scroll pid 9
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_examples --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader
first attach after 27544 ms
client alive at capture: 1
screenshot: 6121 bytes
colours: 1 (sampled 1920x1080)
attaches: 0
      1 xdg_surface#38.get_toplevel
      1 xdg_wm_base#11.get_xdg_surface
client	dur=10	procs=2	threads=61	cpu%=17.30	PSS_MB=120.0	RSS_MB=171.3	ctxsw/s=201.8	FDs=24
```

("first attach after 27544 ms" is the harness's attach-wait loop timing out:
there is no attach to wait for. The `measure.sh` row is the 10 s window taken
while the client sat idle after the settle period; `procs=2` is the FHS
wrapper's bash plus `views_examples`.)

Reading of the run — finding **F2**: stock `views_examples` on Ozone/Wayland
connects, creates an `xdg_toplevel` (one `xdg_wm_base.get_xdg_surface`, one
`xdg_surface.get_toplevel`) and `wl_shm` buffers, and **never attaches one**
(`attaches: 0`), so the screenshot is blank: 6,121 bytes, one sampled colour.
The root cause, settled on the bench: on Show, `WindowTreeHostPlatform`
allocates a new parent `LocalSurfaceId` and `WaylandWindow` latches state (and
sends `xdg_surface.ack_configure`) only when a viz frame reaches that sequence
point; the in-process test sink `ui/compositor/test/direct_layer_tree_frame_sink.cc`
mints its own ids from a private allocator, so every frame carries seq 1,
nothing latches, and `WaylandFrameManager` discards every frame as
unconfigured. The one-line bench fix (debt D1) is
`ui::test::EnableTestConfigForPlatformWindows()` before `aura::Env`
initialises; with it the same harness shows `ack_configure(3)`, 37 attaches in
about 3 s and a fully drawn window — `views-examples-154-headless-scroll.png`
in this directory, 44,521 bytes against the 6,121 blank. The production-faithful
fix belongs to T9. The stock binary measured here carries no such patch: the
`attaches: 0` row above is stock Chromium 154.0.8037.92.

## Footprint

The `measure.sh` row for the stock idle client (process tree, 10 s window):

```text
client	dur=10	procs=2	threads=61	cpu%=17.30	PSS_MB=120.0	RSS_MB=171.3	ctxsw/s=201.8	FDs=24
```

PSS_MB=120.0, RSS_MB=171.3, 61 threads, CPU 17 % of one core while idle —
the CPU is the compositor-frame spin of a client whose frames are discarded
(F2); a drawing client idles lower. Run to run the PSS moved between roughly
120 and 175 MB with no other change.

**Caveat: indicative numbers only.** The bench composites with scroll's pixman
renderer on a headless output and the client renders through SwiftShader
software GL with no `/dev/dri`. Every number on this page (PSS, RSS, CPU,
context switches) is measured on that software stack; on a real seat with GPU
acceleration the footprint and the CPU figures will differ. They are a
baseline to compare `views_shell` against **under the same harness**
(`docs/bench/views-shell-154.md`), not a prediction of production cost.
