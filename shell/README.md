# shell: the views-shell C++ program

This directory becomes `//views_shell` inside a Chromium checkout at the pinned
tag: [`../tools/bench/worker/wire.sh`](../tools/bench/worker/wire.sh) resets the
checkout to pristine, copies `shell/` to `src/views_shell/` and applies
[`patches/series`](patches/series) with plain `git apply`. Everything here is
either built by `BUILD.gn` or applied by that series; prototype code that was
neither has been deleted, and [`PROVENANCE.md`](PROVENANCE.md) says where each
piece went and how to recover it from git history.

The binary is a Views program, not a content embedder: no `//content`, no Blink
renderer, no V8, no `//chrome`, no `//ash` (rules R1, R5, R24), enforced by
`assert_no_deps` in `BUILD.gn`.

## Layout

| Path | What |
|---|---|
| `BUILD.gn` | the `views_shell` executable, the `views_shell_unittests` umbrella and the registered subsystem seams, with the rule guards |
| `app/views_shell_main.cc` | the content-free main, shaped like `ui/views/examples/examples_main_proc.cc`: `--bar` (a top layer surface), `--demo-popup` (an `xdg_popup` menu parented to it), `--left-tabs` (the workspace strip), `--run-for-seconds` |
| `style/`, `wm/`, `ui_tree/`, `plugins/`, `bar/`, `notifications/` | the subsystem seams: each directory has its own `BUILD.gn` reachable from the root one, and is filled by its own item without touching the root |
| `wm/compositor_adapter.h`, `wm/wm_model.h`, `wm/wm_snapshot.h`, `wm/adapters/scroll/` | sketches of the compositor-neutral model and the scroll and sway adapter, replaced by the adapter work |
| `tabs/` | Chrome's tab and vertical-strip layouts, copied (never linked) into a vertical workspace strip; see [`../CHROME-PORT-LEDGER.md`](../CHROME-PORT-LEDGER.md) and [`../docs/tabs.md`](../docs/tabs.md) |
| `patches/` | the live Chromium patch series; see [`patches/README.md`](patches/README.md) |
| `build/` | `CHROMIUM_VERSION` (the pinned tag) and the release configuration for footprint measurement; see [`build/README.md`](build/README.md) |
| `docs/` | the 2026-06-27 niri switcher's behaviour, kept as acceptance tests for the workspace rail |
| `tools/no_stubs.py` | the no-stub check, with good and bad fixtures |

## The shell main, without `//content`

`main()` follows `ui/views/examples/examples_main_proc.cc`: base and the thread
pool, `mojo::core::Init()`, ICU, the `ResourceBundle`, Ozone for Wayland,
`aura::Env`, `wm::WMState`, then the surfaces. The context factory is the one
open engineering item: Chromium has no production `ui::ContextFactory` outside
`//content`, so the program uses the test `TestContextFactories` with
`ui::test::EnableTestConfigForPlatformWindows()` (finding F2, debt D1 in
[`../docs/architecture.md`](../docs/architecture.md)) and is `testonly` until a
production in-process viz host replaces it. `components/viz/demo` is the
precedent for running viz without `//content`.

## Running anything here

Nothing is built or run on a seat. The bench builds and runs the program through
[`../tools/bench/`](../tools/bench/), and every run that launches a compositor
goes through `runtime-test` inside a nested headless scroll
([`../tools/bench/worker/headless.sh`](../tools/bench/worker/headless.sh)).
