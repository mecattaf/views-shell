# views_shell release footprint and dependency census (Chromium 154.0.8037.92)

Measured on the bench on 2026-10-02 by chapter 2 item w1b (claims C10.1 to
C10.5), with the committed sequence `tools/bench/seq/w1b.sh` (unit
`vs-w1b-seq`, log `~/views-bench/logs/w1b-seq.log`, rc 0) under
`~/views-bench/bench.lock`. The procedure is written down in
[`../../shell/build/README.md`](../../shell/build/README.md) (Measuring
footprint).

## Builds

- `out/views` was restored first. The cancelled chapter-1 run had left a
  drifted `args.gn` (no `use_siso = false`, `use_remoteexec = false`,
  `cc_wrapper`, `dcheck_always_on`) and siso state, and no `views_examples`.
  `tools/bench/worker/ensure-out.sh views tools/bench/args.views.gn` copied the
  args back, ran `gn clean` (siso back to ninja), `gn gen` (`ENSURE-OK views
  cleaned`), and `autoninja -C out/views views_shell views_examples` rebuilt
  20,998 steps from ccache, rc 0.
- `out/release` is new: `ensure-out.sh release tools/bench/args.release.gn`
  (`ENSURE-OK release cleaned`), then `autoninja -C out/release views_shell
  views_examples`, 21,572 steps, rc 0. The args are
  `shell/build/args.release.gn` (release, `is_component_build = false`,
  `is_official_build = false` because PGO profiles are not checked out,
  `symbol_level = 0`, `dcheck_always_on = false`) plus the bench lines
  (`use_remoteexec = false`, `use_siso = false`, `cc_wrapper = "ccache"`,
  `enable_ax_automation_bindings = false`, finding F1).
- The wired tree was main at `07c0681` (the five live patches, `views_shell`
  still `testonly = true` with the test context factory, debt D1).
  `views_examples` is the stock target: wire.sh resets the checkout, so it
  carries no F2 test config and attaches no buffer (below).

## Size and dependencies

From `tools/bench/worker/census.sh` (reports in
`~/views-bench/results/w1b-census/`). "stripped" is the binary after the
checkout's `third_party/llvm-build/Release+Asserts/bin/llvm-strip`.
`runtime_deps` is `gn desc <label> runtime_deps`: the files the target declares
it needs at run time, with their total size.

| | release views_shell | release views_examples | component views_shell | component views_examples |
|---|---|---|---|---|
| binary as linked (bytes) | 88,657,416 | 92,416,032 | 29,939,688 | 53,514,296 |
| stripped (bytes) | 63,250,200 | 66,006,808 | 595,024 | 915,184 |
| `ldd` lines | 31 | 31 | 321 | 326 |
| runtime_deps files | 102 | 102 | 437 | 444 |
| runtime_deps bytes | 208,180,146 | 265,952,416 | 1,375,018,946 | 1,465,012,072 |
| `gn desc deps --all` targets | 6,417 | 6,444 | 6,417 | 6,444 |

The component binaries are thin: their code is in the 300-odd shared
libraries `ldd` lists, which is why the stripped component executable is under
one megabyte and its runtime_deps exceed a gigabyte.

The release runtime_deps of `views_shell` are mostly test and diagnostic data
that `testonly` support targets declare, not what the program needs to draw:
`test_fonts/` (51.7 MB, from `//base/test:test_support`),
`libVkLayer_khronos_validation.so` (30.2 MB), `libtest_trace_processor.so`
(15.7 MB), `icudtl.dat` (10.8 MB), the SwiftShader Vulkan ICD (6.7 MB), ANGLE
`libEGL.so`/`libGLESv2.so`, `libvulkan.so.1` and the mock ICD, a fontconfig cache
and `ui/en-US.pak`. The binary plus what a release package would ship
(ANGLE, SwiftShader where no GPU is present, ICU data, the paks) is about
84 MB on disk; the remaining runtime_deps leave with debt D1, when
`views_shell` stops depending on `//base/test:test_support` and
`//ui/compositor:test_support`. `views_examples` additionally lists
`tools/skia_goldctl/linux/goldctl` (55.4 MB), which explains its larger total.

`gn path out/release //views_shell:views_shell <t>` prints `No non-data paths`
for `//v8`, `//content`, `//third_party/blink/renderer/core`,
`//third_party/blink/renderer/platform` and `//chrome`, 5 of 5, as in
`out/views`.

### Census by tree

`gn desc out/release //views_shell:views_shell deps --all`, grouped by the
first path segment (the same counts hold in `out/views`; the dependency graph
does not change with the component setting):

| first segment | targets | | first segment | targets |
|---|---|---|---|---|
| `//third_party` | 3,532 | | `//mojo` | 74 |
| `//ui` | 789 | | `//skia` | 61 |
| `//services` | 654 | | `//net` | 56 |
| `//components` | 504 | | `//url` | 45 |
| `//media` | 238 | | `//cc` | 45 |
| `//gpu` | 134 | | `//ipc` | 25 |
| `//build` | 106 | | `//sandbox` | 21 |
| `//base` | 86 | | `//device` | 18 |

and by the first two segments, largest first: `//third_party/perfetto` 849,
`//third_party/webrtc` 755, `//third_party/xnnpack` 579, `//services/network`
348, `//ui/webui` 313, `//third_party/blink` 301 (all under `blink/public` and
`blink/common`; nothing under `blink/renderer`), `//third_party/abseil-cpp` 197,
`//third_party/rust` 176, `//media/mojo` 124, `//third_party/dawn` 120,
`//ui/base` 113, `//ui/gfx` 112, `//services/viz` 95, `//gpu/ipc` 87,
`//components/metrics` 70, `//mojo/public` 69, `//third_party/wayland-protocols`
68, `//media/capture` 55, `//third_party/ruy` 53, `//services/webnn` 53,
`//ui/accessibility` 52, `//services/device` 50, `//components/content_settings`
38, `//third_party/tflite` 31, `//components/os_crypt` 30. `//ui/views` itself
is 10 targets. `views_shell` and `views_examples` differ by 27 targets
(`views_examples` adds `//ui/message_center`, `//ui/shell_dialogs`,
`//ui/snapshot` and a few more `//ui` targets).

### Reading

A target count is not linked code: most of these targets are mojom bindings,
generated headers and code-generation actions, and the linker keeps only what
is reached. Still, the census names the trees that `views_shell` drags in and
that a tiny Views library would not need. By size of the subtree:

- WebRTC (755 targets), XNNPACK, TFLite, ruy and LiteRT (about 680 together,
  machine learning for `//services/webnn`), media capture, libvpx, libaom and
  `//media/mojo`: they arrive through `//components/viz/service`. `gn path`
  (from `tools/bench/seq/w1b.sh prove`) shows the edges:
  `views_shell → //ui/compositor:test_support → //components/viz/service →
  //media/capture:capture_lib → //third_party/webrtc_overrides:webrtc_component
  → //third_party/webrtc/...` (75 such paths) and
  `... //components/viz/service → //services/webnn:webnn_service →
  //third_party/xnnpack:xnnpack` (8). Today viz/service comes in through the
  test context factory (debt D1), but the production in-process viz host that
  pays D1 needs `//components/viz/service` too, so these edges stay unless
  viz/service's own dependencies on media capture and WebNN are cut, as F3 cut
  the Blink renderer edges.
- Dawn/WebGPU (120): mostly headers, through `//skia:skia_core_and_effects`
  (`//third_party/dawn/include/dawn:cpp_headers`, 222 paths).
- `//ui/webui` (313, WebUI resources and their TypeScript build actions), via
  `//ui/base → //ui/webui/resources:resources_grit` (89 paths): a resource
  bundle dependency of `//ui/base`, not code.
- The network service (`//services/network`, 348; `//net`, 56) and browser
  components seen as mojom (metrics, DWA and UKM, content settings, os_crypt,
  variations, omnibox, autofill, payments, digital goods, schema_org,
  Bluetooth): for example `//ui/aura → //cc → //components/metrics/dwa:dwa_builders
  → //components/metrics/private_metrics → //services/network/public/cpp`.
- Perfetto (849) is tracing, which `//base` links publicly
  (`//base → //third_party/perfetto:libperfetto`, 1,918 paths); a tiny library
  would keep the client and drop the trace processor
  (`libtest_trace_processor.so` is a runtime dep only through test support).

What a Views shell needs is a fraction of the graph: `//ui` (789), `//base`,
`//skia`, `//cc`, `//mojo`, `//gpu` and `//services/viz` for the in-process
compositor, `//third_party/wayland-protocols` and ANGLE/SwiftShader. The
release binary is 63 MB stripped beside 66 MB for `views_examples`: the shell
is the same size as the stock examples program, because both carry the same
viz, GPU and service closure. Shrinking it is a matter of cutting edges in
that closure (the way F3 cut the Blink renderer and V8 edges), not of
`views_shell`'s own code.

## Footprint runs

Same harness for all three rows: `tools/bench/worker/headless.sh` under
`runtime-test`, headless stock scroll with pixman, one 1920x1080 output,
scratch `HOME`, `--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader`
(finding F2), `measure.sh` over 10 s after a 4 s settle; each row counts the
process tree under the `build-env` wrapper (`procs=2`).

Release `views_shell --bar` (results `~/views-bench/results/w1b-release-bar`,
screenshot [`views-shell-154-release-bar.png`](views-shell-154-release-bar.png),
a 32 px bar at the top of the output):

```
client: ~/views-bench/build-env -c ~/chromium/src/out/release/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar
first attach after 5405 ms
client alive at capture: 1
screenshot: 6970 bytes
colours: 3 (sampled 1920x1080)
attaches: 1
      1 zwlr_layer_shell_v1#11.get_layer_surface
      4 zwlr_layer_surface_v1#38.configure
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=81.1	RSS_MB=89.3	ctxsw/s=0.0	FDs=55
```

Release `views_examples` (results `w1b-release-examples`), the non-drawing
control: stock `views_examples` creates its toplevel but never attaches a
buffer on this harness (F2), so the screenshot is the empty output and the
"first attach" line is the harness's wait limit, not an attach:

```
client: ~/views-bench/build-env -c ~/chromium/src/out/release/views_examples --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader
first attach after 26888 ms
client alive at capture: 1
screenshot: 6121 bytes
colours: 1 (sampled 1920x1080)
attaches: 0
      1 xdg_surface#39.get_toplevel
      1 xdg_wm_base#12.get_xdg_surface
client	dur=10	procs=2	threads=61	cpu%=4.60	PSS_MB=111.8	RSS_MB=120.9	ctxsw/s=200.9	FDs=25
```

Component `views_shell --bar` on the restored `out/views` (results
`w1b-views-bar`), the baseline for the same program:

```
client: ~/views-bench/build-env -c ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar
first attach after 2242 ms
client alive at capture: 1
screenshot: 6970 bytes
colours: 3 (sampled 1920x1080)
attaches: 1
      1 zwlr_layer_shell_v1#11.get_layer_surface
      4 zwlr_layer_surface_v1#38.configure
client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=125.3	RSS_MB=135.4	ctxsw/s=0.0	FDs=345
```

| | release views_shell --bar | release views_examples (control) | component views_shell --bar |
|---|---|---|---|
| draws | yes (1 attach, 3 colours) | no (0 attaches) | yes (1 attach, 3 colours) |
| PSS (MB) | 81.1 | 111.8 | 125.3 |
| RSS (MB) | 89.3 | 120.9 | 135.4 |
| CPU at idle (%) | 0.00 | 4.60 | 0.00 |
| context switches/s | 0.0 | 200.9 | 0.0 |
| threads | 30 | 61 | 30 |
| FDs | 55 | 25 | 345 |

The release build takes 44 MB of PSS off the same program (125.3 to 81.1 MB),
because a non-component binary does not map 321 shared libraries with their
relocations and GOTs. The release `views_shell` bar sits 30 MB under the
release `views_examples` control, which has twice the threads and keeps
spinning (200 context switches a second) even though it never draws.

The open chapter-1 question about 344 FDs is answered by measurement: the same
`views_shell` holds 345 FDs as a component build and 55 as a release build. The
difference (290) tracks the shared libraries `ldd` lists (321 against 31), so
the FDs belong to the component build's library loading, not to the shell. The
mechanism (which code keeps one descriptor per library open) is not yet
identified; the release number is the one that describes the program.

The first attach of the release `views_shell` was 5.4 s in this run, the first
after a cold 21,572-step build; the C10.3 rerun of the same binary a few
minutes later (same harness, `w1b.sh prove`) attached after 2.0 s with
`PSS_MB=80.2`, threads 30, FDs 55, colours 3, attaches 1. Treat start-up time
as unmeasured until rows are repeated.

## Caveat

Indicative only, as in chapter 1: the compositor renders with pixman and the
client draws through SwiftShader into `wl_shm` buffers, because `runtime-test`
exposes no `/dev/dri` (F2). One sample per row. A GPU-fair run with a render
node, and N >= 3 samples, will give other numbers. `views_shell` still links
test support (debt D1), which shows in its runtime_deps and may in its PSS.
