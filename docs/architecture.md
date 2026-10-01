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

The carry is small and kept small. Each patch has one job:

| Patch (target name) | Job |
|---|---|
| `views-shell-ozone-layer-shell.patch` | `PlatformWindowType::kLayerShell`, the `layer_shell_*` init properties, the factory case (with the enum-placement fix folded in), global registration, and the virtual `OnKeyboardFocusChanged` that lets a layer window become active |
| `views-shell-ozone-layer-popup.patch` | `xdg_popup` children of layer surfaces, for menus, bubbles and tooltips |
| `views-shell-ozone-empty-opaque-region.patch` | Stops stock `WaylandWindow` from declaring translucent surfaces opaque (the "black box") |
| `views-shell-build-gate.patch` | Root `gn_all` reaches `//views-shell`, plus `BUILDFLAG(ENABLE_VIEWS_SHELL)` |
| `views-shell-dbus-visibility.patch` | Opens `components/dbus` thread visibility so one shared session-bus owner works |

New Ozone files (`wayland_layer_shell.*`, `wayland_layer_shell_window.*`) live in
`shell/ozone/layer_shell/` and are copied into
`ui/ozone/platform/wayland/host/`. The `wlr-layer-shell` protocol XML is vendored
only if the tree's `third_party/wayland-protocols` still lacks it. None of the
patches touches `//content`, so they carry over unchanged to a content-free build.

The data protocols leave Ozone for compositor IPC. The patch that put a whole
browser window on a layer surface is deleted. The current lifts are untrimmed: see
[`../shell/PROVENANCE.md`](../shell/PROVENANCE.md).

## 3. The shell main

A Views program, shaped like Chromium's own `ui/views/examples:views_examples`,
which builds Views with no `//content`, Blink or V8:
- `main()` initialises `base` (a UI `SingleThreadTaskExecutor`, the thread pool,
  one IO thread for D-Bus and sockets), the feature list, `mojo::core::Init()`,
  ICU, and the `ResourceBundle` with a repacked `views-shell.pak` (ui/views resources,
  `ui/strings`, views-shell's own strings and the icon subset) and `locales/<lang>.pak`.
- Ozone is initialised for Wayland in single-process mode.
- **An in-process viz host** gives the compositor its GPU path: views-shell owns a
  production `ui::ContextFactory`, modelled on `ui/compositor`'s
  `InProcessContextFactory` and on `components/viz/demo` (which runs `VizMainImpl`
  without `//content`), without the test providers. This is the one real piece of
  new engineering the content-free shape costs. A GPU fault restarts the shell;
  the systemd user unit brings it back and the layer surfaces reappear. Until that
  factory exists, the executable borrows the test one: see debt D1 below.
- `ShellMainParts` creates `aura::Env` and `wm::WMState`, installs a production
  focus client, `wm::DefaultActivationClient` and a capture client on every
  surface root, a production `ui::AXPlatform` delegate (for Orca), and defers
  `Widget::Show` until those exist.
- `ShellViewsDelegate` installs the layer-surface host on every top-level widget. It
  `CHECK`s that each top-level `Widget` has a registered `SurfaceSpec`. That check
  is how rule R1 is enforced at runtime.
- A `LayoutProvider` and the views-shell `TypographyProvider` are installed at start.

On Linux, Aura is Chrome's in-process window tree (`DesktopWindowTreeHostLinux`
maps one Aura root onto one platform window). It manages no one else's windows:
the compositor is the window manager.

The June 30 spike proved Views on a layer surface (translucency, paint and
keyboard all passed) inside a content embedder. Its four init gaps (layout
provider, views delegate, locale pak, aura clients) do not depend on `//content`.
Of the 248 MB it measured, about 12 MB was Views; the two zygotes and the network
service (114 MB) are the process model of `//content`, which this shape drops.
The lifted embedder in `shell/app/` is provenance, not the plan.

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

## 5. Compositor adapters

One adapter per compositor feeds the compositor-neutral `WmModel`. Each adapter
publishes a static capability table and probes at connect. One config writer per
compositor sits beside each adapter. See
[`compositor-adapters.md`](compositor-adapters.md).

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

## 7. UI-tree renderer

T1 and T2 plugins send a declarative `ui` tree
([`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json)). The
renderer maps each node to a stock Views widget (or one of the two ported
quick-settings faces). Colours are semantic roles, never literals. Events from
the tree go back to the plugin as command invocations. A plugin never receives a
`views::View`. The vocabulary grows only by stock components (for example
`mediaSession`, drawn by `MediaItemUIView`), never by first-party privilege.

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

`//views_shell:views-shell` is an `executable` with

```
assert_no_deps = [ "//chrome/*", "//ash/*", "//chromeos/*",
                   "//content/*", "//third_party/blink/renderer/*", "//v8/*" ]
```

The guard names the Blink *renderer*, not all of Blink:
`components/viz/service` legitimately pulls the small
`//third_party/blink/public/common` types library. The proof, once a tree exists,
is `gn path out/views-shell //views_shell:views-shell //v8` returning nothing. It is built with
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
| D1 | `views_shell` takes its `ui::ContextFactory` from `//ui/compositor:test_support` (the test in-process context factory), so the executable is `testonly = true` and links test support it would not ship | chapter 1, allowed by the orchestrator's ruling of 2026-10-01 | a production in-process viz host exists (`app/views_shell_context_factory.{h,cc}`: `viz::HostFrameSinkManager` and `VizMainImpl` on a GPU thread, after `components/viz/demo`), the target drops `//ui/compositor:test_support` and `//base/test:test_support`, `testonly = false`, and the shell still draws its bar under headless scroll |

While D1 is open, every footprint number measured next to `views_examples` is
indicative only, and the `testonly` flag is the visible marker that the binary is not
shippable.
