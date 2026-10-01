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
  `views-shell-ax-automation-bindings.patch` (F1),
  `views-shell-blink-renderer-edges.patch` (F3, below), then
  `views-shell-build-gate.patch` (root `gn_all` gets `//views_shell` when
  `is_linux && use_ozone`). It prints `WIRE-OK`.
- Unit `t3-build`: `autoninja -C out/views views_shell`, rc 0. The binary is
  29,334,856 bytes. It has 6,412 transitive deps after the F3 patch (6,488
  before); `views_examples` had 6,518 before it.
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
(`out/views/build.ninja` names `//ash` zero times). The C3.3 command therefore
checks the five labels above and counts 5 lines; for `//ash` the `assert_no_deps`
entry `//ash/*` is the guard that holds.

Finding F3 (measured here): at the pinned tag the stock Views stack,
`views_examples` included, reaches `//v8` and `//third_party/blink/renderer`
by two edges:

- `//components/viz/host` and `//components/viz/service` →
  `//components/input` → `//ui/events/blink` and `//ui/events/gestures/blink`
  → `//third_party/blink/public:blink_headers`, which publicly depends on
  `//third_party/blink/renderer/platform:make_platform_generated`, `wtf` and
  `//v8:v8_headers`. That brought in `//third_party/blink/renderer/platform/wtf:wtf`,
  `//v8:v8_libbase`, `//v8:v8_libplatform`, the `cppgc`/`v8` header sets and
  about twenty Blink code-generation actions.
- `//services/webnn/public/mojom:webnn_mojom_traits` →
  `//third_party/blink/renderer/modules/ml:operand_id_hash_traits` → `wtf`.

Neither edge is needed. `//ui/events/blink` includes only
`blink/public/common` headers; `//ui/events/gestures/blink` adds
`blink/public/platform/web_gesture_curve.h`, a header that includes only
`ui/gfx/geometry/vector2d_f.h`. The WebNN C++ variant never uses the WTF hash
traits; only the Blink variant's `HashMap<OperandId, ...>` does.
`shell/patches/views-shell-blink-renderer-edges.patch` cuts both: the two
`ui/events` targets depend on `//third_party/blink/public/common` and a new
header-only `//third_party/blink/public:web_gesture_curve` (also a public dep
of `blink_headers`, so other users see no change), and the hash traits move
from `webnn_mojom_traits` to the Blink variant's typemap.

With the patch, `gn desc out/views //views_shell:views_shell deps --all`
lists no target under `//v8`, `//third_party/blink/renderer`, `//content`,
`//chrome` or `//gin`, and `ldd views_shell` lists no `libv8*` or `libwtf`.
`shell/BUILD.gn` guards the whole trees: `assert_no_deps` names `//ash/*`,
`//chrome/*`, `//chromeos/*`, `//content/*`, `//gin/*`,
`//third_party/blink/renderer/*` and `//v8/*`, and `gn gen out/views`
passes.

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
(results `t3-exit`, `VIEWS_SHELL_EXIT=0`). These rows were taken before the
F3 patch; the rerun after it (same harness) gave 2 attaches, 7 colours and
`PSS_MB=138.7 RSS_MB=150.9`, threads 30, FDs 344.

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

## Layer surface and popup (T4)

Measured on the bench on 2026-10-01 by task T4 (`SPEC.md`, claims C4.2 and
C4.3; PROVE rows P4.2 and P4.3), same harness as above (pixman compositor,
SwiftShader client), with the `views-shell-ozone-layer-shell` and
`views-shell-ozone-layer-popup` patches applied. The lines below are copied from
`~/views-bench/results/t4-bar/summary.txt` and `t4-popup/summary.txt` on the
bench.

`views_shell --bar` (screenshot `views-shell-154-bar.png`): one 32 px top
layer surface with an exclusive zone, namespace `views-shell-bar`.

```
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar
first attach after 3415 ms
client alive at capture: 1
screenshot: 6970 bytes
colours: 3 (sampled 1920x1080)
attaches: 1
      1 zwlr_layer_shell_v1#11.get_layer_surface
      4 zwlr_layer_surface_v1#38.configure
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=154.2	RSS_MB=164.5	ctxsw/s=0.0	FDs=344
```

`views_shell --bar --demo-popup` (screenshot `views-shell-154-popup.png`): the
bar opens a `MenuRunner` menu, which becomes an `xdg_popup` parented to the
layer surface with `zwlr_layer_surface_v1.get_popup`.

```
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar --demo-popup
first attach after 2972 ms
client alive at capture: 1
screenshot: 12290 bytes
colours: 48 (sampled 1920x1080)
attaches: 6
      1 xdg_surface#45.get_popup
      1 xdg_wm_base#12.get_xdg_surface
      1 zwlr_layer_shell_v1#11.get_layer_surface
      5 zwlr_layer_surface_v1#38.configure
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=158.4	RSS_MB=168.9	ctxsw/s=0.0	FDs=346
```

Against the T3 window the bar row shows 154.2 MB PSS (141.3 for T3, a different
binary and patch set), the same 30 threads and the same 344 FDs; the open menu
adds two FDs and about 4 MB. The 344 FDs are explained in the next section.

## File descriptors

Measured on the bench on 2026-10-02 by item w1c with the FD census of
`tools/bench/worker/headless.sh` (`CLIENT_FD_DUMP=1`, `CLIENT_FD_RECHECK=60`):
`out/views` restored to `tools/bench/args.views.gn` and rebuilt from origin/main,
same harness as above (pixman compositor, SwiftShader client), `views_shell --bar`
(results `~/views-bench/results/w1c-fd`) and `views_examples` as the comparison
(`w1c-fd-examples`).

```
fd kinds: socket=2 anon_inode:[eventfd]=5 pipe=2 memfd=3 /dev/shm=0 file=329 other=4
fd other: /dev/null=2 anon_inode:[signalfd]=1 anon_inode:[eventpoll]=1
fd per process: 36:bwrap=4 47:views_shell=341
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=125.2	RSS_MB=135.3	ctxsw/s=0.0	FDs=345
fd kinds (recheck): socket=2 anon_inode:[eventfd]=5 pipe=2 memfd=3 /dev/shm=0 file=329 other=4
fd per process (recheck): 36:bwrap=4 47:views_shell=341
```

`views_examples`, same minute:

```
fd kinds: socket=2 anon_inode:[eventfd]=5 pipe=2 memfd=3 /dev/shm=0 file=9 other=4
fd per process: 36:bwrap=4 48:views_examples=21
client	dur=10	procs=2	threads=61	cpu%=5.10	PSS_MB=158.9	RSS_MB=169.6	ctxsw/s=201.8	FDs=25
```

Owners of the 341 descriptors of the `views_shell` process (`fds-47.txt`):

| Owner | FDs | What |
|---|---|---|
| `base::debug::EnableInProcessStackDumping()` (called in `views_shell_main.cc` `main`) | 321 | `SandboxSymbolizeHelper::OpenSymbolFiles()` in `base/debug/stack_trace_posix.cc` opens, `O_RDONLY`, every read-only executable mapping in `/proc/self/maps` and keeps the descriptor so a crash can symbolize after sandboxing: the executable, 269 component libraries from `out/views` and 51 system libraries (glibc, glib, nss, at-spi, libdrm, mesa libgbm and the rest), fds 4 to 324 |
| `base::WaitableEvent`, message pumps and the thread pool | 5 eventfd, 1 epoll, 1 pipe pair | the same set `views_examples` has |
| `base::SharedMemory` regions | 2 memfd `shared-memory-region` | consistent with the two `wl_shm.create_pool` of the client log (the census does not tie a memfd to a pool); `views_examples` has the same two |
| SwiftShader | 1 memfd `swiftshader_jit` | the JIT code region of the software GL |
| unix sockets | 2 | one is the Wayland connection; both are unnamed on the client side and the census does not name the second peer |
| resources | 3 files | `icudtl.dat` and `ui_test.pak` (opened twice) |
| stdio and the harness | 4 | `/dev/null`, `client.log` twice, `bench.lock` (inherited from the `flock` holding the bench lock; not the client's) |

The remaining process of the tree is the FHS wrapper's `bwrap` (4 FDs: stdio and
a signalfd). The chapter-1 rows (344) are the same 340 plus that `bwrap`; the
341st here is the inherited lock descriptor.

Finding candidate: the 344 FDs are a steady allocation, not a leak. 321 of
them are the stack-symbolization helper's pre-opened module descriptors,
which `views_examples` never creates because its `main` does not call
`EnableInProcessStackDumping()`; without them `views_shell` holds 20
descriptors to `views_examples`' 21 (it lacks `views_examples_resources.pak`). The census
60 seconds later lists the same 341 descriptors of the same kinds. The count
scales with the number of mapped libraries, so a non-component build will
hold far fewer, and the helper only opens files when `OFFICIAL_BUILD` is unset
or unwind tables are kept (`!defined(OFFICIAL_BUILD) ||
!BUILDFLAG(EXCLUDE_UNWIND_TABLES)`). Whether the shell keeps the call is a
choice for the production entry point (debt D1, T9); it costs descriptors, not
memory (the files are already mapped).
