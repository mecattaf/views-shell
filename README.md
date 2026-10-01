# views-shell

**views-shell** is a standalone desktop shell for Wayland, named for what it is: Chromium
Views C++ on `wlr-layer-shell` surfaces (the name was settled on 2026-10-01; see
[`docs/naming.md`](docs/naming.md)).

It is a desktop shell in **Chromium Views C++** on `wlr-layer-shell` surfaces,
built from stock Views controls and a handful of uncoupled Ash components, beside a
compositor that someone else maintains well. scroll is the default compositor, stock and unpatched. The
binary is a Views program, not a browser: it links no `//content`, no Blink
renderer and no V8.

> Status: **foundation**. This repository holds the specs, schemas, the component
> kit inventory, a Chrome extension skeleton and a faithful lift of earlier proven
> code. Nothing here builds a shell yet. See [What is real](#what-is-real-today).
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
docs/                   architecture, rules, adapters, upstream wishes for scroll, the Chromium hop, naming
style/                  views-shell/style: the component kit (inventory, gallery, theme binding)
schemas/                views-shell-plugin.json, the ui tree, keybinding slot rendering, the T2 protocol
examples/               example plugins and example themes that validate
cli/                    the views-shell command line: spec and a dispatch skeleton
extension/              the views-shell Chrome extension (MV3 skeleton, native messaging client)
shell/                  the C++ shell: BUILD.gn sketches and the lifted prior code
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
task table are the source of truth, and its gates are what "done" means. The bench
([`tools/bench/`](tools/bench/)) holds Chromium at the pinned tag `154.0.8037.92`.

- **Real:** the rules, the architecture, the adapter matrix, the plugin and `ui`
  schemas with validating examples (`tools/validate.sh` prints `fences: clean`,
  `identity: ok` and `VALIDATE-OK`), the theme binding and example themes, and the
  extension skeleton (it loads unpacked and shows its pages). The lifted C++ in
  `shell/` is real code that compiled in its original trees at Chromium 148, 149 and
  150. It is copied byte for byte and does not build here yet.
- **Placeholder:** every `BUILD.gn` under `shell/` is a sketch, and the
  content-free shell main is described but not written. The CLI is a dispatch
  skeleton. The native messaging host does not exist yet, so the extension pages
  show a "not installed" state.
- **PROPOSED:** items that wait on a decision are marked **PROPOSED** where they
  appear. [`docs/open-decisions.md`](docs/open-decisions.md) collects them.

## Credits and licence

views-shell is BSD-3-Clause, matching Chromium. It uses code from the Chromium project.
It is not affiliated with Google. Chromium code keeps its original headers. Credit
goes to dawsers for scroll, to the Chromium Authors, to Omarchy for the theme
format, to noctalia for the adapter-per-compositor lineage, and to aurade (Cam396,
BSD-3) as a neighbouring project.
