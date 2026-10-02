# Bluetooth (quick settings): `views-shell.qs-bluetooth`

A T1 plugin with no code: a tile and a device page bound to the first-party
`views-shell.bluetooth` source plugin (BlueZ through `device/bluetooth`, which builds on
Linux).

Pairing a device that needs a PIN or passkey makes BlueZ call its registered
`org.bluez.Agent1`, which is the core credential modal (rule R25), never this
plugin. Opening the page is what starts discovery: the source plugin scans only
while a consumer has the page open, so nothing polls.

## Tier, contributions, capabilities, permissions

- **Tier:** T1 declarative, no commands of its own.
- **Contributes:** `quickSettings` entries `tile` (slot `tile`, default size
  `primary`, order -90, opens `page`) and `page` (title "Bluetooth"), both bound to
  `views-shell.bluetooth/state`; trees [`ui/tile.json`](ui/tile.json) and
  [`ui/page.json`](ui/page.json). The tile calls
  `views-shell.bluetooth/toggle-power`; the page calls `set-powered`,
  `toggle-connection` and `pair`.
- **Capabilities:** none. **Permissions:** `call:views-shell.bluetooth/*`.
  **Depends on:** `views-shell.bluetooth`.
- The `tile` node is not drawn in chapter 2; the page is.

## Registry fixture

[`tools/fixtures/registry/views-shell.qs-bluetooth.json`](../../tools/fixtures/registry/views-shell.qs-bluetooth.json).
Render traces: `tools/fixtures/render/quick-settings-bluetooth.{tile,page}.json`.
