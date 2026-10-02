# Do not disturb (quick settings): `views-shell.qs-dnd`

The smallest quick-settings plugin: one compact tile, no code, no page. Its state
is the notification server's quiet mode (`message_center::MessageCenter::SetQuietMode`
underneath, linked from `ui/message_center`), published by the `views-shell.notifications`
source plugin. The tile calls `views-shell.notifications/toggle-quiet-mode`, which its
`call:` permission names exactly.

## Tier, contributions, capabilities, permissions

- **Tier:** T1 declarative, no commands of its own.
- **Contributes:** one `quickSettings` entry, `tile` (slot `tile`, size
  `compact`, order 10, no page), bound to `views-shell.notifications/state`; tree
  [`ui/tile.json`](ui/tile.json).
- **Capabilities:** none. **Permissions:**
  `call:views-shell.notifications/toggle-quiet-mode`, the exact command, not a
  wildcard. **Depends on:** `views-shell.notifications`.
- The `tile` node is not drawn in chapter 2.

## Registry fixture

[`tools/fixtures/registry/views-shell.qs-dnd.json`](../../tools/fixtures/registry/views-shell.qs-dnd.json).
Render trace: `tools/fixtures/render/quick-settings-dnd.tile.json`.
