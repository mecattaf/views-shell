# Do not disturb (quick settings): `views-shell.qs-dnd`

The smallest quick-settings plugin: one compact tile, no code, no page. Its state
is the notification server's quiet mode (`message_center::MessageCenter::SetQuietMode`
underneath, linked from `ui/message_center`), published by the `views-shell.notifications`
source plugin. The tile calls `views-shell.notifications/toggle-quiet-mode`, which its
`call:` permission names exactly.
