# shell: the views-shell C++ program

This directory becomes `//views-shell` inside a Chromium checkout. It holds two kinds of
files, and the difference matters:

- **Lifted** (real code, never compiled here): byte-for-byte copies of the
  layer-shell, embedder, rail and niri code that worked in earlier prototypes. Each
  is listed with its source and hash in [`PROVENANCE.md`](PROVENANCE.md). They keep
  their original identifiers until a separate rename commit.
- **Sketches** (placeholders): `BUILD.gn` at this level, `wm/compositor_adapter.h`,
  `wm/wm_model.h`, `wm/adapters/scroll/`. They show the intended shape. They have
  never been built.

Nothing here has been built against a Chromium tree. No tree is checked out.

## Layout

| Path | Kind | What |
|---|---|---|
| `BUILD.gn` | sketch | the `views-shell` executable and its rule guards |
| `ozone/layer_shell/` | lifted | `zwlr_layer_shell_v1` binding and the layer-surface window, copied into `ui/ozone/platform/wayland/host/` |
| `ozone/protocol/` | lifted | `wlr-layer-shell-unstable-v1.xml`, vendored only if upstream lacks it |
| `patches/` | lifted | the Ozone and build-gate carry, untrimmed; see `patches/README.md` |
| `app/` | lifted, provenance only | the June 30 spike's `//content` embedder, including the surface table and the Views bootstrap. views-shell links no `//content` (rule R24), so these files are replaced, not adapted; see below |
| `host/` | lifted | the shell host: surface and controller lifetime |
| `rail/` | lifted | the read-only rail mirror (controller and view) |
| `bar/`, `launcher/` | lifted | the bar and clock; the launcher panel (pattern only) |
| `wm/` | sketch | the compositor-neutral model and adapter interface |
| `wm/adapters/niri/` | lifted | the niri IPC client: the template for every adapter |
| `wm/adapters/scroll/` | sketch | the scroll and sway adapter (new code) |
| `tools/` | lifted, one adapted | `headless-eval.sh` (adapted for `runtime-test`), `no_stubs.py` |
| `build/` | lifted, one sketch | `args.gn`, wire scripts, link gate; `args.release.gn` (sketch) for footprint measurement |
| `docs/` | new | the 2026-06-27 switcher's behaviour, as acceptance tests |

## The shell main, without `//content`

The binary is a Views program, not a content embedder. Its `main()` is shaped like
Chromium's own `ui/views/examples/examples_main_proc.cc`, which builds Views with no
`//content`, Blink or V8 (`ui/views/examples/BUILD.gn`, `views_examples_lib`,
`views_examples_proc`, `views_examples`; read at `df5b64d9`):

1. `base` setup: a UI `SingleThreadTaskExecutor`, the thread pool, an IO thread
   for D-Bus and the sockets, `base::FeatureList::InitInstance`.
2. `mojo::core::Init()`, ICU, and the `ResourceBundle` with `views-shell.pak` and the
   locale pak.
3. Ozone for Wayland with `single_process = true`.
4. `aura::Env`, `wm::WMState`, a production `ui::AXPlatform` delegate.
5. **`ShellContextFactory`**: the production `ui::ContextFactory`. `views_examples`
   reaches the screen through `ui/compositor:test_support`
   (`InProcessContextFactory`, `TestContextFactories`), which is `testonly`.
   Outside `//content`, Chromium has no production context factory, so views-shell owns
   one: an in-process viz host after `InProcessContextFactory` without the test
   providers, with `components/viz/demo` (`viz_demo`, not testonly) as the
   precedent for running `VizMainImpl` without `//content`. This is the one real
   engineering item of the content-free shape.
6. `ShellMainParts`, then the shell host and the surfaces.

The cost: a GPU fault takes the shell down instead of a helper process; the
systemd user unit restarts it. Nothing here is written yet.

## First acceptance targets

Two things have no live proof yet, and come first once a tree builds:

1. `xdg_popup` children of a layer surface (menus, bubbles, tooltips).
2. A rail click that switches a workspace, observed through the compositor's echo.

## Running anything here

Any test that launches a compositor, sources a script fragment or touches a
runtime directory runs only as `~/.local/bin/runtime-test -- <cmd>`.
`tools/headless-eval.sh` refuses to run otherwise.
