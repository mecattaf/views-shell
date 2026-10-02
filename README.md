# views-shell

**views-shell** is a standalone desktop shell for Wayland, named for what it is: Chromium
Views C++ on `wlr-layer-shell` surfaces (the name was settled on 2026-10-01; see
[`docs/naming.md`](docs/naming.md)).

It is a desktop shell in **Chromium Views C++** on `wlr-layer-shell` surfaces,
built from stock Views controls and a handful of uncoupled Ash components, beside a
compositor that someone else maintains well. scroll is the default compositor, stock and unpatched. The
binary is a Views program, not a browser: it links no `//content`, no Blink
renderer and no V8.

> Status: **a content-free Views program that runs as a shell on headless stock
> scroll** (chapter 2 closed 2026-10-02). It builds at Chromium `154.0.8037.92`
> with no `//content`, Blink renderer or V8, with a production in-process viz
> host, and draws a themed bar with a live workspace strip, notification popups
> and typed input on layer-shell surfaces under a headless compositor on the
> bench. No seat runs it yet. See [What is real](#what-is-real-today).
> There are no releases before v1; see [Releases](#releases).

views-shell is not a compositor, not a browser fork and not a ChromeOS clone. It is one
program, in the manner of Quickshell, that you run under scroll, sway, niri or
Hyprland. It sits next to your own terminal and your own stock Google Chrome.

## Three faces

Everything views-shell does is reachable through three faces, and all three route to the
same command ids.

1. **Views on layer-shell.** Native Chromium Views widgets on
   `zwlr_layer_surface_v1` surfaces: the bar, the workspace rail, quick settings,
   notifications, the launcher, the window cycle and the on-screen display.
   Menus, bubbles and tooltips are `xdg_popup` children of those surfaces.
2. **The views-shell Chrome extension.** One Manifest V3 extension in stock Google
   Chrome. Its settings and control pages open in ordinary Chrome tabs, much like
   `chrome://settings` for your desktop. It reaches views-shell through Chrome native
   messaging: Chrome starts `views-shell native-host`, nothing listens on a port and
   nothing needs pairing. See [`extension/`](extension/).
3. **The `views-shell` command line.** Terminals, scripts and agents use it. Plugins
   mount their own verbs under `views-shell <plugin> <verb>`. See [`cli/`](cli/).

## Compositors

views-shell is compositor-portable. A compositor-neutral `WmModel` sits in the core. One
adapter per compositor feeds it, an idea that comes from the Noctalia era, and one
config writer per compositor sits beside each adapter. Capabilities are declared,
never assumed.

| Compositor | Status | IPC |
|---|---|---|
| **scroll** (default, tracked at master) | first adapter, reference | i3-ipc framing on `$SCROLLSOCK` |
| sway | rides with scroll | the same i3-ipc on `$SWAYSOCK` |
| niri | before v1 | JSON lines on `$NIRI_SOCKET`, `EventStream` |
| generic (`ext-workspace` + `ext-foreign-toplevel`) | before v1 (PROPOSED order) | Wayland protocols only |
| Hyprland | before v1 | `.socket.sock` requests, `.socket2.sock` events |

See [`docs/compositor-adapters.md`](docs/compositor-adapters.md). scroll is stock and
unpatched (Tom, 2026-10-01); the IPC additions that would help are recorded as
upstream wishes in [`docs/scroll-fork-hooks.md`](docs/scroll-fork-hooks.md), and no
capability depends on one. "Works on scroll, sway, niri and Hyprland" is the claim
only once those adapters ship.

## Plugins

A **plugin** is a views-shell shell module. The model follows Omarchy's plugins and VS
Code's contribution manifest. An **extension** is only ever a Google Chrome
extension, which in practice means the views-shell Chrome extension. See
[`docs/naming.md`](docs/naming.md).

Plugins supply data and handlers, never pixels. They come in four tiers:

| Tier | What | Where it runs |
|---|---|---|
| T0 | built-in C++, first party, in the tree (the source plugins that produce state, and core surfaces) | the views-shell process |
| T1 | declarative manifest, no code | no code runs |
| T2 | any-language subprocess over JSON-RPC on stdio, sending a `ui` tree | a supervised child process |
| T3 | JavaScript host (later) | a separate host process |

Every quick-settings item is a plugin. Network, Bluetooth, audio, brightness, do
not disturb, media and the power footer are T1 plugins written exactly as a third
party would write them, bound to first-party source plugins that draw nothing.
The core keeps only the host, the plugin API and the credential modal.

Third-party plugins carry no C++. They send a declarative UI tree
([`schemas/ui-tree.schema.json`](schemas/ui-tree.schema.json)), and views-shell draws it
with stock Views in the active theme. The stable API is JSON schemas, never a C++
ABI. The manifest is `views-shell-plugin.json`
([`schemas/views-shell-plugin.schema.json`](schemas/views-shell-plugin.schema.json)), with
examples in [`examples/`](examples/).

## The look

Stock Chromium Views components, in the theme you pass. The theme format is
exactly Omarchy's: a theme directory with `colors.toml`. `#000000` is simply the
background of Tom's `noir` theme; a light theme works. Chrome follows the same
theme through the `BrowserThemeColor` managed policy, with no extension theme and
no fork. GTK keeps its own theme (MacTahoe on Tom's seats); views-shell never writes GTK
CSS. See [`style/tokens/README.md`](style/tokens/README.md).

## The rules

The full list, with reasons, is in [`docs/rules.md`](docs/rules.md). In short:

1. Views C++ is drawn **only** on wlr-layer-shell surfaces and their `xdg_popup`
   children. There is never an `xdg_toplevel`, a fullscreen app or a terminal.
2. Fullscreen belongs to other programs: kitty for the terminal, Chrome for the
   browser and every views-shell control page, Nautilus for files. No file selector and
   no file manager in views-shell.
3. **Views first.** Stock Views is the donor; an Ash file is ported only where
   Views has no twin and the file has no window-manager coupling. Ash is never
   linked. Ports are recorded in [`ASH-PORT-LEDGER.md`](ASH-PORT-LEDGER.md) (five
   rows) with their BSD-3 headers kept.
4. The seam law: a view that only draws is a candidate for a port, and its stock
   twin wins. A controller that mutates windows becomes "compositor command, then
   observed echo".
5. Plugins carry no C++ and draw no pixels.
6. Lua in the core is minimal. A plugin may use scroll's Lua only as a declared,
   reviewed permission.
7. Chrome stays stock: one extension, no fork, no literal `views-shell://`, no side
   panel, and native messaging as the only bridge.
8. Colours come from the theme you pass, in Omarchy's format.
9. The binary links no `//content`, Blink renderer or V8. HTML goes to Chrome.
10. Licences of other projects are inspiration only, except the BSD/MIT sources we
    copy with their notices. GPL and LGPL code is read, never copied.
11. Every test that launches a compositor, sources a script fragment or touches a
    runtime directory runs under `runtime-test`.

## Releases

There is no release history. Tom daily-drives `main`. The first release, v1, is
cut when Google ships a Chrome release that Tom names as the signal to freeze: views-shell
hops to that release's Chromium tag, passes every gate, and the plugin manifest,
`ui` tree, protocol, CLI and native messaging contracts freeze with it. See
[`docs/chromium-hop.md`](docs/chromium-hop.md).

## Layout

```
README.md               this file
LICENSE                 BSD-3-Clause
SPEC.md                 the ratified spec of the chapter in progress: claims, rulings, tasks
ASH-PORT-LEDGER.md      the five files ported from Ash, and the stock twins of the rest
CHROME-PORT-LEDGER.md   the tab files copied from //chrome for the workspace strip
docs/                   architecture, rules, adapters, upstream wishes for scroll, the Chromium hop, naming
style/                  views-shell/style: the component kit (inventory, gallery, theme binding)
schemas/                views-shell-plugin.json, the ui tree, keybinding slot rendering, the T2 protocol
examples/               example plugins and example themes that validate
cli/                    the views-shell command line: spec and a dispatch skeleton
extension/              the views-shell Chrome extension (MV3 skeleton, native messaging client)
shell/                  the C++ shell: BUILD.gn, the views_shell main, the live patch series
tools/                  repository checks (schema validation, fences, identity)
tools/bench/            the headless build bench: sync, job units, wire, headless run, measure
```

## The bench

Nothing is built or run on a seat. Chromium is fetched, built and exercised on one
headless build host (the **bench**), reached only through
[`tools/bench/`](tools/bench/): `sync.sh` ships the worktree, `job.sh` starts and
polls long-running units, and `worker/*.sh` run on the bench (wire the tree, build,
run a client under a nested headless scroll inside `runtime-test`, measure a process
tree). The bench pins the Chromium tag in
[`shell/build/CHROMIUM_VERSION`](shell/build/CHROMIUM_VERSION), currently
`154.0.8037.92`, and every run it produces is written up under `docs/bench/`.

## What is real today

The chapter in progress is specified in [`SPEC.md`](SPEC.md): its claims, rulings and
task table are the source of truth, and its gates are what "done" means. Chapter 2
closed on 2026-10-02 (its report for Tom is
[`docs/reports/chapter-2.md`](docs/reports/chapter-2.md)); the next chapter's spec
goes on top of it. The bench ([`tools/bench/`](tools/bench/)) holds Chromium at
the pinned tag `154.0.8037.92`.

**What builds.** `//views_shell:views_shell` is a production Views program
(`testonly = false`, no `test_support` in its dependency graph), built in the
component `out/views` and the non-component `out/release` with
[`shell/BUILD.gn`](shell/BUILD.gn) and the five live patches in
[`shell/patches/`](shell/patches/) (2,048 lines, plain `git apply`). It links no
`//content`, no Blink renderer, no V8, no `//chrome` and no `//ash`, by
`assert_no_deps` and by `gn path` (`No non-data paths`, five trees, both build
directories). Six seams under `shell/` are real code with their own `BUILD.gn`:
`style` (the Omarchy theme directory read, resolved and pinned onto `kColorSys*`),
`wm` (the scroll and sway adapter over i3-ipc, `WmModel`, `wm_probe`), `bar`
(the bar, its workspace strip and clock), `notifications` (a freedesktop
Notifications server on `//dbus` feeding `ui/message_center`), `ui_tree` (the
`ui`-tree renderer onto stock Views), `plugins` (manifest validation, the
registry, the permissions broker, T1 plugins, T2 supervision over JSON-RPC,
`plugin_probe`), plus `tabs` (Chrome's vertical tab strip, copied, never
linked). `views_shell_unittests` collects every seam's tests: 139 ran green on
the bench, against fake sockets, a recorded scroll transcript, a private session
bus and a private headless scroll.

**What draws on stock scroll** (headless, under `runtime-test`, records in
[`docs/bench/`](docs/bench/README.md)): `--bar --theme <omarchy theme dir>` draws
a 32 px top layer surface in the theme's colours (the ground is the theme's
`background` to 99.8 % of its rows: `#1a1a1a`, `#000000`, `#f9f9f7`), with a
workspace strip fed by the compositor over i3-ipc (a workspace switch was sent
and its echo observed, `ECHO workspace 3`), the focused window's title and a
clock; `--demo-popup` opens a menu that becomes an `xdg_popup` parented to the
bar; a `notify-send` becomes its own overlay layer surface, top right, and is
destroyed on expiry; `--demo-keyboard` opens an exclusive-keyboard overlay
surface into which `wtype` typed `hello` (`TYPED hello`); `--left-tabs` draws
the Chrome-derived workspace strip on the left edge; a run with no surface flag
exits 2 (rule R1). The release build of the assembled program is 63 MB
stripped and idles at 59.7 MB PSS, 0 % CPU, 17 threads and 69 FDs (component:
108.7 MB, 362 FDs; GPU-fair with a render node: 128.4 MB). Chapter 1's open
questions are answered: the 344 FDs were `EnableInProcessStackDumping`'s
pre-opened module files of a component build, and the two F2 GL switches are a
consequence of `runtime-test`'s empty `/dev`, not of the shell (with `/dev/dri`
bound, the default GL draws over linux-dmabuf).

**The tools.** `tools/validate.sh` prints `fences: clean`, `identity: ok`, the
schema, theme, registry (12 views), render (14 traces) and `no_stubs` lines,
`CONFORMANCE-OK` for the three T2 example plugins played over stdio by
`tools/plugin-conformance.py`, and `VALIDATE-OK`. `tools/plugin-registry.py` and
`tools/ui-tree-render.py` are the Python references whose goldens the C++
reproduces byte for byte. `PROVE.md` is the append-only evidence table (141
rows at the close of chapter 2), linted by `tools/prove-lint.py` and re-run by
`tools/prove.sh`; the bench is driven by `tools/bench/` (`sync.sh`, `job.sh`,
`lock.sh`, the `worker/` scripts and one `seq/<item>.sh` per bench item).

- **Real, as data:** the rules, the architecture, the adapter matrix, the plugin
  and `ui` schemas with validating examples (12 plugins, 4 themes), the plugin
  protocol settled in [`schemas/plugin-protocol.md`](schemas/plugin-protocol.md),
  the rendering contract in [`schemas/ui-tree-rendering.md`](schemas/ui-tree-rendering.md),
  the plugin authoring guide [`docs/plugins.md`](docs/plugins.md), the theme
  binding, and the extension skeleton (it loads unpacked and shows its pages).
- **Plans, not code:** [`docs/chrome-lift-inventory.md`](docs/chrome-lift-inventory.md)
  and [`docs/shell-composition.md`](docs/shell-composition.md) (what can be
  lifted from Chrome and Ash and how it composes each surface).
- **Not yet:** the own `views_shell.pak` (debt D2; every program loads
  `ui_test_pak`), the renderer bound to the plugin host on a surface, the Ash
  icon subset the examples name, quick settings, the launcher, the credential
  modal proper (only its keyboard risk is proven), the native messaging host
  (nothing extension-shaped until H1 settles `chrome.storage.sync`), the niri,
  generic and Hyprland adapters, the CLI beyond a dispatch skeleton, and the
  Nix packaging for a seat. The debts and their payoff conditions are in
  [`docs/architecture.md`](docs/architecture.md) "Debts".
- **PROPOSED:** items that wait on a decision are marked **PROPOSED** where they
  appear. [`docs/open-decisions.md`](docs/open-decisions.md) collects them.

## Credits and licence

views-shell is BSD-3-Clause, matching Chromium. It uses code from the Chromium project.
It is not affiliated with Google. Chromium code keeps its original headers. Credit
goes to dawsers for scroll, to the Chromium Authors, to Omarchy for the theme
format, to noctalia for the adapter-per-compositor lineage, and to aurade (Cam396,
BSD-3) as a neighbouring project.
