# Architecture

views-shell is one process, `views-shell`, built from a Chromium tree as a **Views program
without `//content` or `//chrome`**. It links no Blink renderer and no V8 (rule
R24). It runs as a user service bound to the graphical session, the same way
under every compositor. This page describes its layers from the bottom up, and
then the pieces that live outside the process.

```
 ┌─────────────────────────────── stock Google Chrome ───────────────────────────────┐
 │  views-shell Chrome extension: options page, control pages in tabs, omnibox, Ctrl+Shift+K │
 │  colours: BrowserThemeColor managed policy (no extension theme)                    │
 └───────────────────────────────┬────────────────────────────────────────────────────┘
                                 │ chrome.runtime.connectNative (stdio; allowed_origins)
                       `views-shell native-host` (started by Chrome)
                                 │ Unix socket
 ┌──────────────────── views-shell (one process, is_linux, no //content) ─────────────────────┐
 │ 8  shell content      bar · rail · quick-settings host · notifications ·            │
 │                       launcher · window cycle · OSD/toasts · credential modal       │
 │ 7  UI-tree renderer   T1/T2 plugin `ui` trees → stock Views widgets                 │
 │ 6  shell framework    SurfaceSpec registry · command registry (ui::ActionItem) ·    │
 │                       plugin registry · permissions broker · settings store ·       │
 │                       source plugins (producers) · theme service · WmModel          │
 │ 5  compositor adapters  scroll/sway · niri · generic ext-workspace · Hyprland       │
 │                         (+ one config writer each)                                  │
 │ 4  views-shell/style         stock Views kit · theme pin mixer (Omarchy input) · 5 ports   │
 │ 3  shell main         main() · ShellMainParts · in-process viz host ·               │
 │                       ShellViewsDelegate (rule guard) · aura clients                 │
 │ 2  Ozone + layer-shell patches (Chromium carry)                                     │
 │ 1  Chromium at the pinned tag                                                       │
 └──────────────┬───────────────────────────────┬──────────────────────────────────────┘
                │ Wayland (layer-shell, xdg_popup)│ compositor IPC
        ┌───────┴─────────────────────────────────┴────────┐
        │ scroll (default) · sway · niri · Hyprland         │
        └───────────────────────────────────────────────────┘
 `views-shell` CLI ──── Unix socket ($XDG_RUNTIME_DIR/views-shell/views-shell.sock) ────▶ layer 6 command registry
 T2 plugin processes ──── JSON-RPC 2.0 on stdio ────▶ layer 6 plugin registry
```

## 1. Chromium

views-shell builds against one pinned Chromium tag. Before v1 there are no releases:
Tom daily-drives `main`, and a hop to a newer tag is run by hand when it is
wanted. v1 is the Chromium tag of the Google Chrome release Tom names as the
signal to freeze. See [`chromium-hop.md`](chromium-hop.md). No Chromium checkout
ever lives in this repository. `shell/` is added to a tree as `//views-shell`.

## 2. Ozone and the layer-shell patches

The carry is small and kept small: five patches, 2,048 lines in all, applied in
[`../shell/patches/series`](../shell/patches/series) order with plain `git apply`
(rule R20) by `tools/bench/worker/wire.sh`. Each has one job:

| Patch (live series) | Lines | Job |
|---|---|---|
| `views-shell-ax-automation-bindings.patch` | 26 | finding F1: the accessibility Automation bindings behind `enable_ax_automation_bindings`, so `//ui/views` stops reaching `//gin` and `//v8` |
| `views-shell-blink-renderer-edges.patch` | 103 | finding F3: cuts the `blink_headers` edge of `//ui/events/{blink,gestures/blink}` and the WebNN mojom-traits edge into the Blink renderer |
| `views-shell-build-gate.patch` | 16 | root `gn_all` reaches `//views_shell` on `is_linux && use_ozone` |
| `views-shell-ozone-layer-shell.patch` | 1,781 | `PlatformWindowType::kLayerShell`, `WaylandLayerShell` and `WaylandLayerShellWindow` under `ui/ozone/platform/wayland/host/`, the vendored `wlr-layer-shell-unstable-v1.xml`, `LayerShellProperties` through `Widget::InitParams::layer_shell` and `DesktopWindowTreeHostLinux`, the factory case, the virtual `OnKeyboardFocusChanged` that activates a layer window (finding F13), and the empty opaque region for translucent windows (the former empty-opaque-region patch, folded in) |
| `views-shell-ozone-layer-popup.patch` | 122 | `xdg_popup` children of layer surfaces for menus, bubbles and tooltips: the xdg-parent walk stops at a layer surface and `XdgPopup::Initialize` parents through `zwlr_layer_surface_v1.get_popup` |

Nothing in the series touches `//content`. The Ozone files live inside the
layer-shell patch; `shell/` carries no Ozone source of its own. The lifted
`dbus-visibility` patch was deleted with the lift (chapter 2, w1e; recover with
`git show 93d691a:shell/patches/agency-dbus-visibility.patch` if a shared
session-bus owner ever needs that edge; today the notifications server opens its
own `//dbus` connection and needs nothing from `//components/dbus`). The patch
that put a whole browser window on a layer surface is gone. What the series was
re-cut from is recorded in [`../shell/PROVENANCE.md`](../shell/PROVENANCE.md).

Known gap in the carry (debt D3): `WaylandLayerShellWindow::SetMargin` exists,
but `Widget::SetBounds` on a layer surface does not reach it, so a restacked
notification popup keeps its old margins.

## 3. The shell main

A Views program, shaped like Chromium's own `ui/views/examples:views_examples`,
which builds Views with no `//content`, Blink or V8. Three files in `shell/app/`:

- `views_shell_main.cc` keeps the flags (`--bar`, `--left-tabs`, `--theme <dir>`,
  `--demo-popup`, `--demo-workspace-switch`, `--demo-keyboard`,
  `--run-for-seconds`, `--software-compositing` / `--gpu-compositing`) and
  nothing else. A run with no surface flag exits 2 with the rule-R1 message
  before it connects to anything.
- `shell_bootstrap.{h,cc}` (`ShellBootstrap`) brings the process up in this
  order and tears it down in reverse, so later chapters add content without
  re-reading it:
  1. `base`: the feature list, a UI `SingleThreadTaskExecutor`, the thread pool,
     then the `ui::AXPlatform` delegate (native APIs only, for Orca);
  2. Ozone for Wayland in single-process mode: `InitializeForUI` (the Wayland
     connection, threaded event polling), then `InitializeForGPU`;
  3. mojo: `mojo::core::Init()` and a `ScopedIPCSupport` on a mojo IO thread;
  4. ICU and fonts;
  5. the discardable allocator: `discardable_memory::DiscardableSharedMemoryManager`,
     in process, as a browser process has it;
  6. the `ResourceBundle` (still `ui_test_pak`: debt D2);
  7. the context factory (below);
  8. `aura::Env` with that factory;
  9. the input method (`ui::InitializeInputMethod()`);
  10. the desktop screen;
  11. `ShellViewsDelegate` (the kit's `ShellLayoutProvider` and
      `ShellTypographyProvider`; every top-level `Widget` is a
      `DesktopNativeWidgetAura`; `OnBeforeWidgetInit` `CHECK`s every top-level
      widget and every layer-shell request against the `SurfaceSpec` registry
      of `surface_spec.{h,cc}`, rule R1's second enforcement) and `wm::WMState`.
- `shell_content.{h,cc}` (`ShellContent`) owns everything above the bootstrap
  with an explicit `Start` and `Stop`, in this order: the theme (`--theme`,
  applied before the first widget), the scroll adapter and its `WmModel`
  (on `$SCROLLSOCK`, `$SWAYSOCK` or `$I3SOCK`; without one it logs once and
  runs on), the notification daemon (when `$DBUS_SESSION_BUS_ADDRESS` is set),
  then the surfaces. `surface_spec.{h,cc}` is the registry: `bar`, `left-tabs`,
  `notification` and `modal`. `keyboard_probe_view.{h,cc}` is the credential
  modal's keyboard risk, a `views::Textfield` on the `modal` row.
- `views_shell_context_factory.{h,cc}` is **the in-process viz host**, the one
  real piece of new engineering the content-free shape costs. Its service side
  runs on a GPU main thread of its own (plus a GPU IO thread), as the GPU thread
  of `components/viz/service/main/viz_main_impl.cc` does in a GPU process:
  `gpu::GpuInit::InitializeInProcess`, a `viz::GpuServiceImpl` (shaped after
  `components/viz/demo/service/demo_service.cc`), Ozone's GPU-side interfaces
  (on Wayland the buffer manager) and a `viz::VizCompositorThreadRunnerImpl`
  that owns `viz::FrameSinkManagerImpl` on the viz compositor thread. Its host
  side, on the UI thread, is a `viz::HostFrameSinkManager` bound to that
  manager over mojo and one `gpu::GpuChannelHost` to the in-process service.
  Each `ui::Compositor` gets a root frame sink through
  `HostFrameSinkManager::CreateRootCompositorFrameSink` (its
  `AcceleratedWidget`, a `DisplayPrivate`, a `viz::HostDisplayClient`) and a
  `cc::mojo_embedder::AsyncLayerTreeFrameSink` on the client end, the pattern of
  `content/browser/compositor/viz_process_transport_factory.cc` (read, never
  linked). The root sink forwards the compositor's parent `LocalSurfaceId` to
  `viz::Display`, so `WaylandWindow` latches its configure sequence and acks it
  (finding F2) with no test platform-window configuration.
  `--software-compositing` draws with viz's software renderer into the Ozone
  canvas surface (`wl_shm` buffers), with GL disabled in the process;
  `--gpu-compositing` runs `SkiaRenderer` over GL (ANGLE). Both draw on the
  headless bench (`docs/bench/views-shell-154-production.md`); software is the
  default, because it needs no GL, no render node and no GPU context, costs half
  the threads and about 20 MB less PSS, and is the mode that cannot fail on a
  seat without a usable GPU. A lost viz connection ends the process; the systemd user unit brings it
  back and the layer surfaces reappear.

Still to come in this layer: a production focus client,
`wm::DefaultActivationClient` and a capture client on every surface root, the
own pak (debt D2), and the credential modal proper on the `modal` row.

On Linux, Aura is Chrome's in-process window tree (`DesktopWindowTreeHostLinux`
maps one Aura root onto one platform window). It manages no one else's windows:
the compositor is the window manager.

The June 30 spike proved Views on a layer surface inside a content embedder at
248 MB, of which the two zygotes and the network service (114 MB) were the
process model of `//content`. The content-free shape measured in chapter 2 is
59.7 MB PSS as a release build with the adapter, the model, the notification
daemon and the bar running (`docs/bench/views-shell-154-assembled.md`).

## 4. views-shell/style

The public component kit: stock `ui/views` controls under one include root, the
`kColorSys*` system roles with one theme pin mixer appended last, the views-shell layout
and typography providers, and five Ash ports (FeatureTile, QuickSettingsSlider,
the key chips, an icon subset and the `string_matching` ranker). See
[`../style/INVENTORY.md`](../style/INVENTORY.md).

### The theme

The input is exactly Omarchy's theme input: a theme directory with `colors.toml`,
`icons.theme`, `backgrounds/` and optionally `shell.toml`. views-shell resolves
`colors.toml` with a port of Omarchy's cascade and applies it in three layers
([`../style/tokens/README.md`](../style/tokens/README.md)): a seed in every
`ColorProviderKey` (the same seed Chrome receives), exact pins on `kColorSys*`
from [`../style/theme-map.json`](../style/theme-map.json), and per-surface
sections from `shell.toml`. `views-shell theme apply` re-reads the theme, rebuilds the
mixers, calls `ColorProviderManager::ResetColorProviderCache()` and notifies
`ui::NativeTheme` observers, so every view gets `OnThemeChanged()` live. The
black ground is simply the `background` of the `noir` theme.

**Real today (chapter 2, w2b):** layers 1 and 2 are code in `shell/style/`
(`theme_directory`, `omarchy_cascade`, `theme_mixer`, the `theme_map_table` GN
action over 29 real `ui::kColorSys*` ids, the two providers), proven by
`views_shell --bar --theme <dir>` drawing the theme's `background`
(`docs/bench/views-shell-154-theme.md`) and by byte fixtures made from Omarchy's
own resolver. Layer 3 (`shell.toml`), the `theme apply` IPC and the search path
(P18) are not written; the Ash ports of §4 are not ported yet (A040 is the one
the examples already need, finding F16). See
[`../style/tokens/README.md`](../style/tokens/README.md) "Real today".

## 5. Compositor adapters

One adapter per compositor feeds the compositor-neutral `WmModel`. Each adapter
publishes a static capability table and probes at connect. One config writer per
compositor sits beside each adapter. See
[`compositor-adapters.md`](compositor-adapters.md).

**Real today (chapter 2, w2a and w3c):** the scroll and sway adapter
(`shell/wm/adapters/scroll/`), `WmModel` and `wm_probe` exist and are tested
against a recorded transcript; the bar's workspace strip is the first consumer
of the model, redrawing only on the echo (rule R6). niri, the generic
ext-workspace adapter and Hyprland are tables only.

## 6. Shell framework

- **SurfaceSpec registry.** One row per surface: layer, anchors, exclusive zone,
  margins, keyboard interactivity, namespace, output pinning, and the size rule.
  A `DesktopWindowTreeHostLinux` subclass consumes the row through
  `AddAdditionalInitProperties`. Surface lifetime is separate from view lifetime,
  so Esc unmaps the surface, not only the inner widget. Runtime setters cover
  width, exclusive zone, margin and hide.
- **WmModel.** Outputs, workspaces, windows, focus, fullscreen and urgency. It is
  a read-mostly mirror: views-shell observes and requests, and is never a second writer
  of window state. It is keyed on the compositor's session-monotonic id when the
  adapter offers one (scroll's node ids; a fork's `uid`), and otherwise on the
  pair of adapter window id and `ext-foreign-toplevel-list` identifier, never on
  workspace names alone. It diffs adapter snapshots into "children added,
  removed, moved" callbacks after Chrome's vertical-tab collection pattern. MRU
  order is recovered at start from the compositor's focus stacks.
- **Command registry.** One command id is reachable from every face. It is built
  on `ui::ActionItem`, so every quick-settings tile is also a palette row and a
  launcher entry. `when` and enablement clauses are evaluated by views-shell over config
  keys, capabilities and session state, never by a shell predicate.
- **Plugin registry.** Validates `views-shell-plugin.json` with the same validator the
  `views-shell plugin validate` verb uses. Infers activation from contributions.
  Supervises T2 processes (full snapshots, backoff).
- **Permissions broker.** Shows permissions at enable and enforces them for T2,
  including `call:` (another plugin's commands) and `state:read:` (another
  plugin's source).
- **Settings store.** `views-shell.json` in the user's config: presence means enabled,
  built-ins appear only as deviations, runtime state lives under
  `$XDG_STATE_HOME/views-shell`.
- **Source plugins (producers).** One producer per domain (`views-shell.audio`,
  `views-shell.network`, `views-shell.bluetooth`, `views-shell.power`, `views-shell.brightness`,
  `views-shell.session`, `views-shell.media`, `views-shell.notifications`, tray, polkit, the launcher
  index), each over an existing daemon. Each is a built-in (T0) plugin that draws
  nothing: it publishes full snapshots as `contributes.sources` with its own JSON
  Schema, and a set of commands. Disabling a consumer plugin removes a tile;
  disabling a source removes the domain.
- **Theme service.** Reads the theme directory, owns the mixers and the live
  reload (§4), and writes the request file the Chrome policy writer watches
  ([`../style/tokens/README.md`](../style/tokens/README.md)). It never needs root.

**Real today (chapter 2):** the `SurfaceSpec` registry (`shell/app/surface_spec`,
four rows, enforced by `CHECK`), `WmModel` (`shell/wm`), the plugin registry
with the capability gate, the permissions broker, T1 plugins and T2 supervision
(`shell/plugins`, [`plugins.md`](plugins.md), the protocol settled in
`schemas/plugin-protocol.md`), and the theme service's first half
(`ThemeController` in `shell/style`). Not yet: the command registry on
`ui::ActionItem`, the settings store, the source plugins, `when` and enablement
evaluation, `picker/query`, and the binding of the plugin host's `Delegate` to
the renderer and the surfaces (the next chapter's first task).

## 7. UI-tree renderer

T1 and T2 plugins send a declarative `ui` tree
([`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json)). The
renderer maps each node to a stock Views widget (or one of the two ported
quick-settings faces). Colours are semantic roles, never literals. Events from
the tree go back to the plugin as command invocations. A plugin never receives a
`views::View`. The vocabulary grows only by stock components (for example
`mediaSession`, drawn by `MediaItemUIView`), never by first-party privilege.

**Real today (chapter 2, w2d and w3a):** `shell/ui_tree` parses and checks the
tree in C++, resolves bindings and formats, draws onto stock Views with no
colour literal, and traces what it built; the trace equals the Python
reference's goldens byte for byte (`tools/ui-tree-render.py`,
`tools/fixtures/render/`, the contract in `schemas/ui-tree-rendering.md`).
What stock Views cannot draw is a render error with a reason (`tile`,
`tabSlider`, `keyChips`, `mediaSession`, `markdown`, the icons that only Ash
ships). See [`../shell/ui_tree/README.md`](../shell/ui_tree/README.md).

## 8. Shell content

The surfaces, in their dependency order: OSD and toasts; notifications (a
freedesktop Notifications server feeding `ui/message_center` and its stock
notification views); the workspace rail and the bar; the quick-settings host and
the credential modal; the picker and launcher; the window cycle (Alt+Tab); the
cheat sheet; and the T2 host.

The rail is the hero surface. It follows Chrome's vertical tabs: a WorkspaceNode
tree with the animating layout manager. Its collapsed form is 56 DIP wide and keeps
a 56 DIP exclusive zone. The expanded flyout is a separate overlay surface with no
zone, so hovering never makes the compositor re-tile.

**The quick-settings host** is small, because stock Views does the work and every
item is a plugin. The core ships the host (one overlay layer surface, a
`BoxLayoutView` of slider rows, a scrolling `TableLayoutView` tile grid, cards and
a footer, and a page stack with a host-drawn header), the `quickSettings`
contribution point and the permission checks. Network, Bluetooth, audio,
brightness, do not disturb, media and the power footer are seven T1 plugins
(`examples/quick-settings-*`), bound to the source plugins of §6. The host owns
everything a plugin must never own: open and close, focus and Esc, the page
stack, ordering, the "edit tiles" state and the crash badge on a dead T2 entry.
Ash's `QuickSettingsView` is pattern only.

**The credential modal** is the session's NetworkManager secret agent, BlueZ
agent and polkit agent in one queue, on an overlay layer surface with exclusive
keyboard, built from `ui::DialogModel` with a password field. It is core, never
a plugin, and the only password field in the shell (rule R25).

**Real today (chapter 2, w2b, w2c, w3c):** the bar (`shell/bar`: the workspace
strip on the model, the focused window's title, the clock), the notifications
server and its popups as overlay layer surfaces (`shell/notifications`,
[`notifications.md`](notifications.md)), and the keyboard probe on the `modal`
row (typed input proven, `docs/bench/views-shell-154-assembled.md`). The rail
(`--left-tabs`, `shell/tabs`) still reads a static list or niri, not the model.
Not yet: quick settings, the launcher, the window cycle, OSD, the cheat sheet,
and the credential modal proper.

## 9. The native messaging host

The Chrome extension reaches views-shell through Chrome native messaging, not a network
service. The host is the CLI in a mode, `views-shell native-host`, declared by a manifest
file that Home Manager writes to `~/.config/google-chrome/NativeMessagingHosts/`
(and `~/.config/chromium/NativeMessagingHosts/`), with `allowed_origins` naming the
pinned extension id. Chrome starts the host per `connectNative` port; the port is
bidirectional and keeps the extension's service worker alive. The host reads
length-prefixed JSON on stdin, relays typed verbs to the command registry over
`$XDG_RUNTIME_DIR/views-shell/views-shell.sock`, and passes pushes back (`bindings-changed`,
`reload`, `config-error`, `wm`, `open`). Nothing listens on a port and nothing
needs pairing: installing the host manifest is the pairing. Raw compositor
commands and Lua evaluation are never exposed. See
[`../extension/README.md`](../extension/README.md).

The views-shell process is the single writer of compositor config slots and the single
source of live change events: it relays the adapters' reload and binding events
and watches the slot files with inotify.

**Not built** (chapter 2 ruling): nothing extension-shaped until H1 settles
whether `chrome.storage.sync` works for the unpacked extension.

## Outside the process

- **The views-shell Chrome extension** ([`../extension/`](../extension/)): options and
  control pages in tabs, an omnibox keyword, and an in-Chrome command.
- **The `views-shell` CLI** ([`../cli/`](../cli/)): talks to the command registry over the
  Unix socket. Help, completions and `--json` output are generated from manifests.
- **Config slots** ([`../schemas/keybinding-rendering.md`](../schemas/keybinding-rendering.md)):
  the user's dotfiles include generated files. views-shell writes only those files.
- **The Chrome policy writer**: a system `.path` unit and a root writer that turn
  the theme service's request file into `BrowserThemeColor` managed policy. It
  lives in the dotfiles, not here (open decision P19).

## Build shape

`//views_shell:views_shell` is an `executable` (`testonly = false`) with

```
assert_no_deps = [ "//chrome/*", "//ash/*", "//chromeos/*",
                   "//content/*", "//third_party/blink/renderer/*", "//v8/*" ]
```

The guard names the Blink *renderer*, not all of Blink:
`components/viz/service` legitimately pulls the small
`//third_party/blink/public/common` types library. The proof, once a tree exists,
is `gn path out/views //views_shell:views_shell //v8` printing `No non-data paths`
(it does, for `//v8`, `//content`, both Blink renderer trees and `//chrome`, in
`out/views` and `out/release`). The root `BUILD.gn` registers six seams
(`bar`, `notifications`, `plugins`, `style`, `ui_tree`, `wm`), each with its own
`BUILD.gn`, a `source_set` the program links and a `:unittests` set that the one
`test("views_shell_unittests")` collects; `wm_probe` and `plugin_probe` are the
seams' console run gates. It is built with
`target_os = "linux"`, Ozone Wayland only, and `dcheck_always_on = false` set
explicitly. A second, release non-component configuration exists for footprint
measurements, with `views_examples` built beside it as the control. See
[`../shell/build/README.md`](../shell/build/README.md).

## Debts

A debt is a known gap between what the architecture says and what the tree does. It
is recorded here with its payoff condition, and it is paid inside the chapter that
names it or the next one. Nothing else may diverge from this page silently.

| Id | Debt | Carried since | Paid when |
|---|---|---|---|
| D1 | `views_shell` took its `ui::ContextFactory` from `//ui/compositor:test_support` (the test in-process context factory), so the executable was `testonly = true` and linked test support it would not ship | chapter 1, allowed by the orchestrator's ruling of 2026-10-01 | **paid** in chapter 2 item w1a (branch `w/w1a`, commit 7a48fe1): `app/views_shell_context_factory.{h,cc}` is the production in-process viz host (§3), the target drops `//ui/compositor:test_support` and `//base/test:test_support` (`gn desc ... deps --all` names no `test_support`), `testonly = false`, `ui::test::EnableTestConfigForPlatformWindows()` is gone (F2 resolved the faithful way), and the bar, the popup and the plain window draw under headless scroll |
| D2 | the `ResourceBundle` loads `ui_test_pak` (`//ui/resources:ui_test_pak`, a plain `copy()` on Linux, not `testonly`) instead of a repacked `views_shell.pak` (ui/views resources, `ui/strings`, views-shell's own strings and the icon subset) with `locales/<lang>.pak` | chapter 2 (w1a) | views-shell repacks its own pak with a `repack` target under `//views_shell`, the bootstrap loads it by path, and the executable drops the `ui_test_pak` data dep |
| D3 | a restacked `message_center` popup keeps its margins: `Widget::SetBounds` on a layer surface resizes it but does not move it, and the layer-shell patch's `WaylandLayerShellWindow::SetMargin` has no Widget or `PlatformWindow` path (finding F14) | chapter 2 (w2c) | the layer-shell patch gains a margin update through `PlatformWindow`, `ShellMessagePopupCollection` re-margins on restack, and a headless run with two popups shows no gap after the first closes |
| D4 | every seam with Views tests repeats the once-per-process setup (`mojo::core::Init`, `GLSurfaceTestSupport::InitializeOneOff`, `ui_test.pak`, `TestDiscardableMemoryAllocator`, `AXPlatformForTest`) that `ViewsTestSuite` would do, because the umbrella's main is `//base/test:run_all_unittests`; running two seams' display tests in one launcher batch could call `mojo::core::Init` twice | chapter 2 (w2c, w3a) | `views_shell_unittests` gets one shared test main or environment owned by the root `BUILD.gn`, and the per-seam one-shots are deleted |
| D5 | `GetServerInformation` answers the constant `views-shell chapter 2` as its version, because the wired copy of `shell/` carries no git metadata | chapter 2 (w3c) | the build stamps the commit (a GN action over `shell/build/` or an argument `wire.sh` passes) and the server reports it |

Footprint numbers measured while D1 was open (chapter 1) were indicative only.
The chapter-2 numbers (`docs/bench/views-shell-154-production.md`,
`views-shell-154-release.md`, `views-shell-154-gpu.md`,
`views-shell-154-assembled.md`) are of the shippable shape; the software rows
stay under the pixman/SwiftShader caveat and the GPU-fair rows do not. The
index is [`bench/README.md`](bench/README.md).
