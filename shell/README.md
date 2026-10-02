# shell: the views-shell C++ program

This directory becomes `//views_shell` inside a Chromium checkout at the pinned
tag: [`../tools/bench/worker/wire.sh`](../tools/bench/worker/wire.sh) resets the
checkout to pristine, copies `shell/` to `src/views_shell/`, mirrors `style/`,
`schemas/`, `examples/` and `tools/fixtures/` to `src/views_shell/data/`, and
applies [`patches/series`](patches/series) with plain `git apply`. Everything
here is either built by a `BUILD.gn` or applied by that series; the prototype
code that was neither was deleted in chapter 2 (47 files, commit `0d30e89`), and
[`PROVENANCE.md`](PROVENANCE.md) says where each piece went and how to recover it
from git history.

The binary is a Views program, not a content embedder: no `//content`, no Blink
renderer, no V8, no `//chrome`, no `//ash` (rules R1, R5, R24), enforced by
`assert_no_deps` in `BUILD.gn` and checked by `gn path` on the bench.

## Layout

| Path | What |
|---|---|
| `BUILD.gn` | the `views_shell` executable (`testonly = false`), the `tabs` source set, the list of registered seams, the rule guards, and the one `test("views_shell_unittests")` that collects every seam's `:unittests` set. It is the only `BUILD.gn` an item edits to add a seam; a seam fills its own directory |
| `app/views_shell_main.cc` | the flags, nothing else: `--bar`, `--left-tabs`, `--theme <dir>`, `--demo-popup`, `--demo-workspace-switch`, `--demo-keyboard`, `--run-for-seconds`, `--software-compositing` (default) / `--gpu-compositing`. No surface flag: the rule-R1 message and exit 2 |
| `app/shell_bootstrap.{h,cc}` | the process bring-up in order (base, Ozone for Wayland single-process, mojo, ICU and fonts, the discardable allocator, the `ResourceBundle` (debt D2), the context factory, `aura::Env`, the input method, the screen, `ShellViewsDelegate` and `wm::WMState`), torn down in reverse |
| `app/views_shell_context_factory.{h,cc}` | the production in-process viz host (`docs/architecture.md` §3): `GpuInit::InitializeInProcess`, `GpuServiceImpl` and the viz compositor thread on a GPU main thread; `HostFrameSinkManager`, a `GpuChannelHost` and one root `CompositorFrameSink` plus `AsyncLayerTreeFrameSink` per compositor on the UI thread |
| `app/shell_content.{h,cc}` | everything above the bootstrap, with `Start`/`Stop`: the theme, the scroll adapter and `WmModel`, the notification daemon, the surfaces |
| `app/surface_spec.{h,cc}` | the `SurfaceSpec` registry (`bar`, `left-tabs`, `notification`, `modal`) that `ShellViewsDelegate::OnBeforeWidgetInit` `CHECK`s every top-level widget and layer-shell request against (rule R1) |
| `app/keyboard_probe_view.{h,cc}` | the credential modal's keyboard risk: a `views::Textfield` on the `modal` row that logs `TYPED <text>` |
| `style/` | the style kit: `theme_directory` (the `colors.toml` subset), `omarchy_cascade` (Omarchy's resolver, MIT), `theme_mixer` (the seed and the one pin mixer; `ThemeController`), `gen_theme_map.py` (the GN action from `style/theme-map.json` to `ui::ColorId`s), the layout and typography providers |
| `wm/` | `compositor_adapter.h`, `wm_snapshot.h`, `wm_model.{h,cc}`, `adapters/scroll/` (the i3-ipc client and the scroll and sway adapter, with `testdata/scroll-transcript.jsonl` and its recorder), `wm_probe.cc` (the console run gate) |
| `bar/` | `BarView` (left section, centred title, `ClockView`) and `WorkspaceStrip` (one stock button per workspace, redrawn on the model's echo only) |
| `notifications/` | the freedesktop Notifications server on its own `//dbus` connection, `ShellMessagePopupCollection` (popups as overlay layer surfaces), `NotificationService`; see [`../docs/notifications.md`](../docs/notifications.md) |
| `ui_tree/` | the `ui`-tree parser, bindings and formats, the renderer onto stock Views, `ListItemView`, the render trace; contract in `schemas/ui-tree-rendering.md`; see [`ui_tree/README.md`](ui_tree/README.md) |
| `plugins/` | manifest validation, the registry view, the plugin registry, the permissions broker, T1 and T2 plugins, `PluginHost`, `plugin_probe.cc`; see [`plugins/README.md`](plugins/README.md) |
| `tabs/` | Chrome's tab and vertical-strip layouts, copied (never linked) into a vertical workspace strip; see [`../CHROME-PORT-LEDGER.md`](../CHROME-PORT-LEDGER.md) and [`../docs/tabs.md`](../docs/tabs.md). It still reads a static list or niri, not `WmModel` |
| `patches/` | the live Chromium patch series, five patches; see [`patches/README.md`](patches/README.md) |
| `build/` | `CHROMIUM_VERSION` (the pinned tag) and `args.release.gn`, the release configuration for footprint measurement; see [`build/README.md`](build/README.md) |
| `docs/` | the 2026-06-27 niri switcher's behaviour, kept as acceptance tests for the workspace rail |
| `tools/no_stubs.py` | the no-stub check (wired into `tools/validate.py`), with good and bad fixtures |

Every seam's `BUILD.gn` has the same shape: a `source_set` the program links, a
`:unittests` `source_set` the umbrella collects, `assert_no_deps` on the browser
trees, and `data` for what its tests read through `base::DIR_SRC_TEST_DATA_ROOT`
(the mirror under `//views_shell/data`). `wm` and `plugins` also assert no
`//ui/views` dependency and ship a console run gate.

## Tests

`out/views/views_shell_unittests` runs every seam's tests from one binary
(139 at the close of chapter 2). Pure tests run anywhere. Tests that build a
`views::Widget`, a `views::Textfield` or click a ripple need a Wayland display
on this Wayland-only Ozone build, so on the bench they run inside
`runtime-test` against a private headless scroll (`tools/bench/seq/w2c.sh`
stage `display` is the pattern), and they skip with a message without one. The
bus tests of `notifications` need `dbus-run-session`, started outside the FHS
build environment and wrapping it. Each seam repeats the once-per-process
Views setup for now (debt D4).

## Running anything here

Nothing is built or run on a seat. The bench builds and runs the program through
[`../tools/bench/`](../tools/bench/): one `seq/<item>.sh` per item as a transient
unit under the bench lock, and every run that launches a compositor goes through
`runtime-test` inside a nested headless scroll
([`../tools/bench/worker/headless.sh`](../tools/bench/worker/headless.sh)). The
records are indexed in [`../docs/bench/README.md`](../docs/bench/README.md).
