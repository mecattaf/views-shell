# Brightness (quick settings): `example.quick-settings-brightness`

A display-brightness slider as a T2 process plugin. This is the path a third party
takes when no first-party source plugin covers its domain, and it shows that a
subprocess in any language reaches the same `slider` slot as the first-party
entries. A process entry names no `ui` file in its manifest: the program sends its
tree at runtime with `surface/setTree`, where `surface` is the entry id
(`slider`). The tree it sends is the one in [`ui/slider.json`](ui/slider.json).

It owns no policy. It reads `/sys/class/backlight` and sets every DRM backlight
through `brightnessctl -n 1`, the floor the dotfiles `brightness` script keeps. On
the Zenbook Duo, setting `intel_backlight` is enough, because the dock daemon
mirrors eDP-1's level onto eDP-2. The coordinator's two LG 5K displays have no DRM
backlight, so the slider stays hidden there: the tree's `visible` binds
`/available`, which the snapshot sets false without a backlight. DDC through
`ddcutil` is not covered by this example. It never polls: it re-reads after each
set and whenever views-shell sends it a message.

## Tier, contributions, capabilities, permissions

- **Tier:** T2 process (`bin/quick-settings-brightness`, python3 standard
  library, protocol 1). Its id is `example.*`: process plugins may not use the
  reserved `views-shell.*` namespace.
- **Contributes:** commands `set` (`value`, a number from 0 to 1) and `state`
  (`result: json`), and one `quickSettings` entry, `slider` (slot `slider`, order
  -80, no `when`).
- **Capabilities:** none. **Permissions:** `exec:brightnessctl` (the broker runs
  it, the program never does) and `path:read:/sys/class/backlight/*`. Helper:
  `brightnessctl` from nixpkgs.

## Checked by

- Registry view: [`tools/fixtures/registry/example.quick-settings-brightness.json`](../../tools/fixtures/registry/example.quick-settings-brightness.json)
  (activation `onCommand:` for both commands and `onQuickSettings:slider`).
- Render trace: `tools/fixtures/render/quick-settings-brightness.slider.json`.
- Protocol: `tools/plugin-conformance.py examples/quick-settings-brightness`
  prints `CONFORMANCE-OK example.quick-settings-brightness` in
  `tools/validate.sh`. On a host without a DRM backlight the runner's
  `set` call issues no `exec`, so the broker path is only covered where a
  backlight exists.

It has never run against a views-shell process, because the C++ plugin host does
not exist yet. Any test that runs it against a live session goes through
`runtime-test`.

Manifest change in chapter 2 (w3d): the entry had `when:
"!config.views-shell.qs-brightness.enabled"`, a key of a plugin that exists
nowhere, so the clause hid nothing; hiding is the tree's `visible` binding. The
`when` is gone and the description no longer refers to a second copy of the
program.
