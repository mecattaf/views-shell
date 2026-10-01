# Power footer (quick settings): `views-shell.qs-power-footer`

The quick-settings footer as a T1 plugin with no code, in the host's `footer` slot.
It shows the battery and power profile from the first-party `views-shell.power` source
plugin (UPower, power-profiles-daemon), opens the views-shell control pages in Chrome
through a declarative `open` handler, and calls `views-shell.session` for lock and the
power menu (a `views::MenuRunner` menu built from the session plugin's commands).
Lock is a verb; unlock is not one anywhere.
