# views_shell beside views_examples on headless stock scroll (Chromium 154.0.8037.92)

Measured on the bench on 2026-10-01 by task T3 (`SPEC.md`, claims C3.1 to C3.5).
Both programs ran under the same harness, `tools/bench/worker/headless.sh` under
`runtime-test` (headless stock scroll, `WLR_RENDERER=pixman`, one 1920x1080
output). Both ran with `--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader`
(finding F2).

## Build

- Tag `154.0.8037.92`, `out/views` (component build) with the args of
  `tools/bench/args.views.gn`, including `enable_ax_automation_bindings = false`
  (finding F1).
- `tools/bench/worker/wire.sh` (unit `t3-wire`) copies `shell/` to
  `src/views_shell/` and applies `shell/patches/series` with plain `git apply`:
  `views-shell-ax-automation-bindings.patch` (F1), then
  `views-shell-build-gate.patch` (root `gn_all` gets `//views_shell` when
  `is_linux && use_ozone`). It prints `WIRE-OK`.
- Unit `t3-build`: `autoninja -C out/views views_shell`, rc 0. The binary is
  29,334,856 bytes. It has 6,488 transitive deps; `views_examples` has 6,518.
  `ldd` lists 321 libraries for `views_shell` and 326 for `views_examples`.
- `views_shell` is `testonly = true` under debt D1: its context factory is
  `ui::TestContextFactories` from `//ui/compositor:test_support`. Before
  `aura::Env` opens the Wayland connection, it calls
  `ui::test::EnableTestConfigForPlatformWindows()` (finding F2).
- The `views_examples` binary measured here is the bench build that carries the
  same one-line F2 test config in `examples_main_proc.cc`, so both programs draw.
  Stock `views_examples` attaches nothing (F2).

## Dependency check (C3.3)

`gn path out/views //views_shell:views_shell <t>` prints `No non-data paths`
for `//v8`, `//content`, `//third_party/blink/renderer/core`,
`//third_party/blink/renderer/platform` and `//chrome`. For `//ash` it prints
`ERROR Label not found. //ash:ash not found.`: `ash/BUILD.gn` begins with
`assert(is_chromeos)`, so no `//ash` target exists in an `is_linux` build
(`out/views/build.ninja` names `//ash` zero times).

Finding F3 (measured here): the stock Views stack, `views_examples` included,
reaches these through `//components/viz/host` → `//components/input` →
`//ui/events/blink` → `//third_party/blink/public:blink_headers`:

- three shared libraries: `//third_party/blink/renderer/platform/wtf:wtf`,
  `//v8:v8_libbase` and `//v8:v8_libplatform`;
- the header-only source sets `//v8:cppgc_headers`, `//v8:v8_headers`,
  `//v8:v8_config_headers` and `//v8:v8_version`;
- about twenty Blink code-generation actions under `renderer/core`,
  `renderer/platform` and `renderer/bindings`.

The V8 engine (`//v8:v8`, `v8_base`, `v8_snapshot`), `//gin`, Blink `core`,
`platform` and `modules`, and `//content` are not reached. A whole-tree
`assert_no_deps` on `//v8/*` or `//third_party/blink/renderer/*` therefore
fails at `gn gen` for any Views program unless that edge is cut upstream. The
guard in `shell/BUILD.gn` names `//chrome/*`, `//ash/*`, `//chromeos/*`,
`//content/*` and `//gin/*` whole. For V8 and Blink it names the engine and
renderer targets: `//v8:v8`, `v8_base`, `v8_base_without_compiler`,
`v8_compiler`, `v8_initializers`, `v8_snapshot`, `cppgc_base`, Blink `core`,
`modules` and `platform`, `controller/*`, `bindings/core/*` and `bindings/modules/*`.

## Runs

`views_shell` (results `~/views-bench/results/t3-shell`, screenshot
`views-shell-154-headless-scroll.png`):

```
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader
first attach after 2432 ms
client alive at capture: 1
screenshot: 8125 bytes
colours: 7 (sampled 1920x1080)
attaches: 2
      1 xdg_surface#37.get_toplevel
      1 xdg_wm_base#11.get_xdg_surface
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=141.3	RSS_MB=153.6	ctxsw/s=0.1	FDs=344
```

`scrollmsg -t get_tree` lists `"app_id": "views-shell"`, and the client log shows
two `xdg_surface.ack_configure` and four `wl_shm.create_pool`. The window is
tiled to the whole output and shows a black ground with the label
`views-shell` in white. With `--run-for-seconds=3` the program exits 0
(results `t3-exit`, `VIEWS_SHELL_EXIT=0`).

`views_examples` (results `~/views-bench/results/t3-examples`, same harness, same minute):

```
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_examples --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader
first attach after 2290 ms
client alive at capture: 1
screenshot: 44521 bytes
colours: 65 (sampled 1920x1080)
attaches: 37
      1 xdg_surface#38.get_toplevel
      1 xdg_wm_base#11.get_xdg_surface
client	dur=10	procs=2	threads=61	cpu%=8.50	PSS_MB=175.5	RSS_MB=190.5	ctxsw/s=203.5	FDs=24
```

## Reading

| | views_shell | views_examples |
|---|---|---|
| PSS (MB) | 141.3 | 175.5 |
| RSS (MB) | 153.6 | 190.5 |
| CPU at idle (%) | 0.00 | 8.50 |
| context switches/s | 0.1 | 203.5 |
| threads | 30 | 61 |
| FDs | 344 | 24 |

Both rows count the process tree under the `build-env` wrapper (`procs=2`).
At idle `views_shell` uses no CPU: one static label, nothing animates. It uses
about 34 MB less PSS and half the threads. `views_examples` keeps redrawing
(37 attaches against 2), and its examples window starts many controls. The
`views_shell` FD count of 344 is not yet explained and is worth looking at
before the `out/release` footprint build.

Caveat: these numbers are indicative only. The compositor renders with pixman,
and the client draws through SwiftShader (software GL) into `wl_shm` buffers. The
build is a component build, so PSS counts the shared libraries. A seat with a
render node and a non-component `out/release` build will give other numbers; the
release footprint build is the next chapter's first task.
