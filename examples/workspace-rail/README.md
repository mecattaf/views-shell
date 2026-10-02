# Workspace rail: `views-shell.workspace-rail`

**Tier:** T0 built-in (`runtime.mode: "builtin"`, target `//views_shell/rail:rail`).
Built-in mode is reserved for `views-shell.*` ids. The C++ target does not exist
yet: chapter 2 has the compositor-neutral `WmModel` (`shell/wm/`) the rail will
read, but no rail.

The hero surface, after Chrome's vertical tabs: workspaces as groups, windows as
rows, click to focus, rename in place, collapse to free the screen. Its manifest
still declares everything, so the CLI, the extension and the slot files see it
like any other plugin.

## What it contributes

| Contribution | Entries |
|---|---|
| surfaces | `rail` (kind `bar`, `keyboard: none`: the collapsed rail with its exclusive zone) and `flyout` (kind `overlay`, anchored to `surface:rail`, no zone, so hovering never re-tiles) |
| commands | `focus-workspace`, `focus-window`, `rename-workspace` and `move-window` (each gated by `enablement` on its capability), `toggle-collapsed`, `toggle` (a surface verb), `state` (`result: json`) |
| configuration | `collapsedWidth` (56), `expandedWidth` (240), `expandOnHover`, `showWindowCount`, `showScrollColumns`, `outputs` (scope `instance`) |
| keybindings | `toggle-collapsed` on `Mod+Grave`; the slot file is [`../../cli/testdata/scroll/views-shell.workspace-rail.conf`](../../cli/testdata/scroll/views-shell.workspace-rail.conf) |
| menus | `rename-workspace` in `rail/workspace`, `move-window` in `rail/window`, each with a `when` on its capability |
| cli | `views-shell rail toggle\|collapse\|focus\|rename\|state` |
| events | `workspace`, `window`, `output` |

## Capabilities and permissions

- Required: `outputs.list`, `workspaces.list`, `workspaces.focus`,
  `windows.list`, `windows.focus`. A compositor adapter without one of them does
  not get a rail.
- Optional: `workspaces.rename`, `windows.move-to-workspace`, `windows.urgency`,
  `scroll.scroller`. Each optional one switches a command or a decoration on;
  none is a compositor name (rule R14).
- Permissions: none. A built-in runs in the views-shell process.

## Checked by

- Registry view: [`tools/fixtures/registry/views-shell.workspace-rail.json`](../../tools/fixtures/registry/views-shell.workspace-rail.json).
- Slot goldens: `cli/testdata/{scroll,niri,hyprland}/`.

Manifest change in chapter 2 (w3d): `runtime.target` was
`//views_shell/shell/rail:rail`, a label that can never exist, because the
repository's `shell/` directory is the `//views_shell` root in a Chromium tree. It
is now `//views_shell/rail:rail`. The registry view does not carry the target, so
the fixture did not change.
