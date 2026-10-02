# Power menu: `example.power-menu`

**Tier:** T2 process (`runtime.mode: "process"`, `bin/power-menu`, python3 with
the standard library only, protocol 1). The id is `example.*`, not `views-shell.*`:
a process plugin may never take a reserved id.

A bar button whose panel offers lock, log out, suspend, hibernate, reboot and shut
down. The program sends a full snapshot and two `ui` trees and handles commands;
views-shell draws everything. Inspired by Omarchy's `blackcode.power-menu` (MIT;
nothing copied).

## What it contributes

| Contribution | Entries |
|---|---|
| surfaces | `button` (bar widget, right section, `keyboard: none`) and `panel` (panel anchored to `surface:button`, `keyboard: on-demand`); the trees are [`ui/button.json`](ui/button.json) and [`ui/panel.json`](ui/panel.json), sent at runtime with `surface/setTree` |
| commands | `toggle` (a surface verb on `panel`, handled by views-shell and never sent to the program), `lock`, `logout`, `suspend`, `hibernate` (enabled by `session.canHibernate`), `reboot`, `shutdown`, `status` (`result: json`); `logout`, `reboot` and `shutdown` carry `confirm: true`, so views-shell always asks first |
| configuration | `showUptime` (boolean, default true) and `hotkey` (a keybinding, default `Mod+Shift+Escape`) |
| keybindings | `toggle` on `keyFrom: config.hotkey`, `lock` on `Mod+Ctrl+L`; rendered into [`../../cli/testdata/`](../../cli/testdata/) for scroll, niri and Hyprland |
| menus, launcher, cli | `toggle` in `views-shell.menu/system`; launcher rows for suspend, reboot and shut down; `views-shell power-menu toggle\|lock\|suspend\|reboot\|shutdown\|status` |

## Capabilities and permissions

- Required capability: `session.exit` (log out goes through the compositor
  adapter). On a compositor without it the plugin is not registered.
- Permissions: `exec:loginctl` (lock), `exec:systemctl` (suspend, hibernate,
  reboot, power off) and `compositor:session.exit` (log out). Each command maps to
  exactly one broker request in the program's `BROKER` table; nothing else is
  reachable. Lock is a verb; unlock is not one anywhere (rule R4).

## Checked by

- Registry view: [`tools/fixtures/registry/example.power-menu.json`](../../tools/fixtures/registry/example.power-menu.json)
  (`python3 tools/plugin-registry.py examples/power-menu --check tools/fixtures/registry/example.power-menu.json`).
- Render traces: `tools/fixtures/render/power-menu.button.json` and
  `power-menu.panel.json` against `tools/fixtures/snapshots/power-menu.json`. The
  panel's shortcut chips are a `keyChips` node, which is not drawn in chapter 2.
- Protocol: `tools/plugin-conformance.py examples/power-menu` prints
  `CONFORMANCE-OK example.power-menu` in `tools/validate.sh`. The runner answers
  the broker requests with canned results and runs nothing.

It has never run against a views-shell process: the C++ plugin host does not
exist yet. Any test that runs it against a live session goes through
`runtime-test`.

Manifest changes in chapter 2 (w3d): the `button` surface now says
`keyboard: none` (the default, `on-demand`, would let a click on a bar button
take keyboard focus), and the `confirm` configuration property is gone (the
program never read it, and `confirm: true` on the commands is fixed in the
manifest, so turning the property off could never have changed anything).
