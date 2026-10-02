# Power footer (quick settings): `views-shell.qs-power-footer`

The quick-settings footer as a T1 plugin with no code, in the host's `footer` slot.
It shows the battery and power profile from the first-party `views-shell.power` source
plugin (UPower, power-profiles-daemon), opens the views-shell control pages in Chrome
through a declarative `open` handler, and calls `views-shell.session` for lock and the
power menu (a `views::MenuRunner` menu built from the session plugin's commands).
Lock is a verb; unlock is not one anywhere.

## Tier, contributions, capabilities, permissions

- **Tier:** T1 declarative.
- **Contributes:** the command `open-settings` with the declarative handler
  `{open: "shortcuts"}` (the extension's default control page, the one
  `views-shell open` opens), and one `quickSettings` entry, `footer` (slot
  `footer`), bound to `views-shell.power/state`; tree
  [`ui/footer.json`](ui/footer.json). The profile label reads the
  `views-shell.power/profiles` source through a binding's `source`.
- **Capabilities:** none. **Permissions:** `call:views-shell.session/lock` and
  `call:views-shell.session/power-menu`, the two exact commands.
  **Depends on:** `views-shell.power`, `views-shell.session`.
- Every node here is drawn in chapter 2.

## Registry fixture

[`tools/fixtures/registry/views-shell.qs-power-footer.json`](../../tools/fixtures/registry/views-shell.qs-power-footer.json).
Render trace: `tools/fixtures/render/quick-settings-power-footer.footer.json`.

Manifest change in chapter 2 (w3d): the handler was `{open: "settings"}`.
`settings` is not a control page (the pages are `shortcuts`, `appearance`,
`plugins`, `compositor`, `notifications` and `connection`), and the options page
falls back to `shortcuts` for an unknown name, so the manifest now names the
page it actually opens.
