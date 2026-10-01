# Brightness (quick settings): `example.quick-settings-brightness`

A display-brightness slider as a T2 process plugin. This is the path a third party
takes when no first-party source plugin covers its domain, and it shows that a
subprocess in any language reaches the same `slider` slot as the first-party
entries. A process entry names no `ui` file in its manifest: the program sends its
tree at runtime with `surface/setTree`, where `surface` is the entry id
(`slider`).

It owns no policy. It reads `/sys/class/backlight` and sets every DRM backlight
through `brightnessctl -n 1`, the floor the dotfiles `brightness` script keeps. On
the Zenbook Duo, setting `intel_backlight` is enough, because the dock daemon
mirrors eDP-1's level onto eDP-2. The coordinator's two LG 5K displays have no DRM
backlight, so the slider stays hidden there; DDC through `ddcutil` is not covered
by this example. It never polls: it re-reads after each set and whenever views-shell sends
it a message.

The JSON-RPC loop answered `initialize`, `command/invoke` and `shutdown` correctly
when fed JSON lines on stdin. It has never run against a views-shell process, because none
exists yet. Any test that runs it against a live session goes through
`runtime-test`.
