# views_shell with its production in-process viz host (Chromium 154.0.8037.92)

Measured on the bench on 2026-10-02 by chapter-2 item w1a (T9, claims C9.1 to
C9.4, debt D1). Everything ran in one bench sequence, `tools/bench/seq/w1a.sh`,
as the transient unit `vs-w1a-seq` under `~/views-bench/bench.lock`; the runs
used `tools/bench/worker/headless.sh` under `runtime-test` (headless stock
scroll, `WLR_RENDERER=pixman`, one 1920x1080 output) with
`--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader` (finding F2;
no `/dev/dri` inside `runtime-test`). Chapter 1's record, with the test context
factory, is `views-shell-154.md`.

## What changed

- `shell/app/views_shell_context_factory.{h,cc}`: the production
  `ui::ContextFactory`, an in-process viz host (`docs/architecture.md` §3). GPU
  main thread: `gpu::GpuInit::InitializeInProcess`, `viz::GpuServiceImpl`,
  Ozone's GPU-side interfaces, `viz::VizCompositorThreadRunnerImpl` with
  `FrameSinkManagerImpl`. UI thread: `viz::HostFrameSinkManager` and one
  `gpu::GpuChannelHost`; per compositor a root `CompositorFrameSink` from
  `CreateRootCompositorFrameSink` and a `cc::mojo_embedder::AsyncLayerTreeFrameSink`.
- `shell/app/shell_bootstrap.{h,cc}`: the process bring-up, split out of main.
- Gone from the program: `ui::test::EnableTestConfigForPlatformWindows()`,
  `base::test::AllowCheckIsTestForTesting()`, `base::TestDiscardableMemoryAllocator`
  (now `discardable_memory::DiscardableSharedMemoryManager`),
  `ui::InitializeInputMethodForTesting()` (now `ui::InitializeInputMethod()`),
  `ui::TestContextFactories`, and the `//base/test:test_support` and
  `//ui/compositor:test_support` deps. `views_shell` is `testonly = false`.
- `shell/{bar,notifications,plugins,style,ui_tree,wm}/BUILD.gn`: empty registered
  seams, and `test("views_shell_unittests")` over their `:unittests` sets.

## Build

`wire.sh` printed `WIRE-OK 334b65d254 28 changed paths`; `gn gen out/views`
made 34154 targets from 5037 files; `gn check out/views '//views_shell/*'`
printed `Header dependency check OK`; `autoninja -C out/views views_shell
views_shell_unittests` rc 0. `views_shell` is 21,848,424 bytes (component
build), `views_shell_unittests` 3,612,760 bytes. The sequence's restore step
(copy `tools/bench/args.views.gn`, `gn clean` on drift or siso state) found
`out/views/args.gn` already matching and no siso state in w1a's runs: another
wave-1 item had restored `out/views` first.

## Dependency check (C9.1)

`gn desc out/views //views_shell:views_shell testonly` prints `false`, and
`gn desc ... deps --all` (6,023 lines with the build-env banner) contains
`test_support` zero times. `gn path out/views //views_shell:views_shell <t>`
prints `No non-data paths found between these two targets.` for `//v8`,
`//content`, `//third_party/blink/renderer/core`,
`//third_party/blink/renderer/platform` and `//chrome` (5 of 5): the viz
service, the GPU service and `//services/viz/public/cpp/gpu` add no path to
them with the F1 and F3 patches applied. `assert_no_deps` is unchanged and
`gn gen` passes it.

## Runs

| Run | Flags | First attach | Alive | Attaches | Colours | Protocol | PSS MB | RSS MB | Threads | FDs |
|---|---|---|---|---|---|---|---|---|---|---|
| `w1a-bar` | `--bar` (software) | 2209 ms | 1 | 1 | 3 | 1 `get_layer_surface`, 4 layer `configure`, 4 `ack_configure` | 107.1 | 117.4 | 15 | 352 |
| `w1a-popup` | `--bar --demo-popup` (software) | 2200 ms | 1 | 12 | 44 | 1 `get_popup`, 1 `get_layer_surface`, 5 layer `configure`, 6 `ack_configure` | 108.6 | 119.2 | 15 | 354 |
| `w1a-plain` | none (software) | 2105 ms | 1 | 2 | 7 | 1 `get_toplevel`, 2 `ack_configure` | 113.6 | 129.6 | 15 | 352 |
| `w1a-bar-gpu` | `--bar --gpu-compositing` | 2145 ms | 1 | 1 | 3 | 1 `get_layer_surface`, 4 layer `configure`, 4 `ack_configure` | 129.0 | 139.0 | 33 | 364 |
| `w1a-plain-gpu` | `--gpu-compositing` | 2122 ms | 1 | 2 | 7 | 1 `get_toplevel`, 2 `ack_configure` | 142.8 | 154.6 | 33 | 364 |

The measure rows are `measure.sh client 10 <pid>` over the client's process
tree (the build-env wrapper and `views_shell`; CPU 0.00 % idle in every run).
Screenshots: `views-shell-154-production-bar.png` (the 32 px bar, black with
the white label) and `views-shell-154-production-popup.png` (the bar with the
demo menu, an `xdg_popup` on the layer surface, drawn with its rounded corners
and shadow by the software renderer).

The client log says which mode came up: `in-process viz up: software
compositing, GL renderer ''` by default, and with `--gpu-compositing`
`in-process viz up: gpu compositing, GL renderer 'ANGLE (Google, Vulkan 1.3.0
(SwiftShader Device (Subzero) (0x0000C0DE)), SwiftShader driver-5.0.0)'`.

## Which compositing mode draws

Both. Software compositing (`root_params->gpu_compositing = false`, GL disabled
in the process, `gpu::GrContextType::kNone`) presents through viz's software
output device on the Ozone canvas surface (`wl_shm`); GPU compositing
(`SkiaRenderer` on ANGLE over SwiftShader Vulkan) presents through the same
Wayland surface. Software is the default: it needs no GL, no render node and no
Skia GPU context, starts half the threads (15 against 33) and costs about 20 MB
less PSS here. `--gpu-compositing` stays selectable.

## Against chapter 1

Chapter 1 (test context factory, GPU path, `views-shell-154.md`): 141 MB PSS,
30 threads, 344 FDs. Now: 107 MB PSS and 15 threads in software, 129 MB and 33
threads in GPU mode. The FD count did not fall (352 software, 364 GPU), so the
344-FD question of chapter 1 is not the test factory; it stays open (w1c owns
it). All numbers carry chapter 1's caveat: pixman in the compositor and
SwiftShader in the client make them indicative only.

## Findings

- F2 is resolved the faithful way: with the root `CompositorFrameSink`
  forwarding the compositor's parent `LocalSurfaceId` to `viz::Display`,
  `WaylandWindow` latches its configure sequence and sends `ack_configure` (4
  for the bar, 6 with the popup) with no test platform-window configuration.
- F-candidate: outside tests, `discardable_memory::DiscardableSharedMemoryManager`
  `CHECK`s at construction that a `base::MemoryConsumerRegistry` exists
  (`base/memory_coordinator/memory_consumer.cc:59`, "The MemoryConsumerRegistry
  did not exist at the time the MemoryConsumerRegistration for
  DiscardableSharedMemoryManager was created"). The bootstrap installs
  `base::DummyMemoryConsumerRegistry`, base's placeholder for standalone
  programs outside `//content`.
- F-candidate: `VizCompositorThread::Init()` calls
  `base::allocator::ReconfigureSchedulerLoopQuarantineBranch`, which `PA_CHECK`s
  a valid PartitionAlloc thread cache (`partition_root.cc:2014`); a process that
  never ran `PartitionAllocSupport`'s reconfiguration crashes there on viz
  start. The bootstrap calls `ReconfigureEarlyish`,
  `ReconfigureAfterFeatureListInit` and `ReconfigureAfterTaskRunnerInit` with
  the browser's process type `""`, as `content/app/content_main_runner_impl.cc`
  does.
- F-candidate: with `--use-gl=disabled`, `gpu::GpuInit::InitializeInProcess`
  `LOG(FATAL)`s "All gr_context_type fallbacks exhausted" unless the
  preferences ask for `GrContextType::kNone`; software mode sets it.

## Unit tests (C9.4)

`out/views/views_shell_unittests` rc 0: `WARNING: No matching tests to run.`
then `SUCCESS: all tests passed.` The umbrella links every seam's empty
`:unittests` set; the chapter-2 items fill them.
