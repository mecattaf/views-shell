# Audio (quick settings): `views-shell.qs-audio`

Two entries in the host's `slider` slot (output and microphone) and a routing
`page`, as a T1 plugin with no code over the first-party `views-shell.audio` source plugin
(an in-process PipeWire and WirePlumber observer, the same one the volume OSD
reads). The slider rows are the ported `QuickSettingsSlider`; the mute toggle is
the slider's `iconAction`. The per-application mixer stays pavucontrol.
