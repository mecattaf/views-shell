# Music scratchpad: `example.music-scratchpad`

**Tier:** T1 declarative (`runtime.mode: "declarative"`): no program, no code.
Everything it does is a declared handler or a declared file.

One key (`F9`) and one bar button summon the music player from the scratchpad.
On scroll it also moves the player to the scratchpad when it first maps, with a
small Lua callback.

## What it contributes

| Contribution | Entries |
|---|---|
| commands | `toggle`, with the declarative handler `{compositor: "scratchpad.toggle", args: {match: {app_id: "music"}}}`: views-shell sends the typed compositor verb itself |
| surfaces | `button` (bar widget, right section, `keyboard: none`) drawn from [`ui/button.json`](ui/button.json), an `iconButton` whose action is `toggle` |
| keybindings | `toggle` on `F9`: [`../../cli/testdata/scroll/example.music-scratchpad.conf`](../../cli/testdata/scroll/example.music-scratchpad.conf) holds `bindsym F9 nop views-shell example.music-scratchpad toggle` |
| compositor | `scroll.lua: ["scroll/scratch_on_map.lua"]`, rendered as a `lua` line in the same scroll slot file so it re-runs after every reload |

## Capabilities and permissions

- Required capability `scratchpad.toggle`; optional capability `scroll.lua`. On a
  compositor without scroll's Lua the plugin still registers, and only the
  map callback is missing.
- Permissions `compositor:scratchpad.toggle` and `scroll.lua`. Lua runs inside the
  compositor, so the schema requires both the capability and the permission for a
  plugin that ships Lua, and the permission is shown at enable (rule R13). Runtime
  `LUA_EVAL` is a separate permission (`scroll.lua.eval`) this plugin does not ask
  for.

## Checked by

- Registry view: [`tools/fixtures/registry/example.music-scratchpad.json`](../../tools/fixtures/registry/example.music-scratchpad.json)
  (activation `onCommand:example.music-scratchpad/toggle`, `onKeybinding:F9`,
  `onStartup`).
- Render trace: `tools/fixtures/render/music-scratchpad.button.json`.
- Slot goldens: `cli/testdata/{scroll,niri,hyprland}/`, re-rendered by
  `tools/validate.sh`.

The Lua file follows the API shape in scroll's `TUTORIAL.md` and has not been
run. Manifest change in chapter 2 (w3d): the `button` surface now says
`keyboard: none`, where the default (`on-demand`) would let a click on a bar
button take keyboard focus.
