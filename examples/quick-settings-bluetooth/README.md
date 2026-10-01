# Bluetooth (quick settings): `views-shell.qs-bluetooth`

A T1 plugin with no code: a tile and a device page bound to the first-party
`views-shell.bluetooth` source plugin (BlueZ through `device/bluetooth`, which builds on
Linux).

Pairing a device that needs a PIN or passkey makes BlueZ call its registered
`org.bluez.Agent1`, which is the core credential modal (rule R25), never this
plugin. Opening the page is what starts discovery: the source plugin scans only
while a consumer has the page open, so nothing polls.
