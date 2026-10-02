# Audio (quick settings): `views-shell.qs-audio`

Two entries in the host's `slider` slot (output and microphone) and a routing
`page`, as a T1 plugin with no code over the first-party `views-shell.audio` source plugin
(an in-process PipeWire and WirePlumber observer, the same one the volume OSD
reads). The slider rows are the ported `QuickSettingsSlider`; the mute toggle is
the slider's `iconAction`. The per-application mixer stays pavucontrol.

## Tier, contributions, capabilities, permissions

- **Tier:** T1 declarative, no commands of its own.
- **Contributes:** `quickSettings` entries `output` and `input` (slot `slider`,
  orders -100 and -90, each opening `devices`) and `devices` (slot `page`, title
  "Audio"), all bound to `views-shell.audio/state`; trees
  [`ui/output.json`](ui/output.json), [`ui/input.json`](ui/input.json) and
  [`ui/page.json`](ui/page.json). They call `set-output-volume`,
  `toggle-output-mute`, `set-input-gain`, `set-default-output`,
  `set-default-input` and `open-mixer` on `views-shell.audio`.
- **Capabilities:** none. **Permissions:** `call:views-shell.audio/*`.
  **Depends on:** `views-shell.audio`.
- Every node here is drawn in chapter 2 (sliders, radio groups, a list item).

## Registry fixture

[`tools/fixtures/registry/views-shell.qs-audio.json`](../../tools/fixtures/registry/views-shell.qs-audio.json).
Render traces: `tools/fixtures/render/quick-settings-audio.{output,input,page}.json`.
