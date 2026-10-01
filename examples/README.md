# Example plugins

Each example validates against [`../schemas/views-shell-plugin.schema.json`](../schemas/views-shell-plugin.schema.json),
and each `ui/*.json` against [`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json).
Run `tools/validate.sh` from the repository root.

| Example | Tier | Shows |
|---|---|---|
| [`power-menu/`](power-menu/) | T2 process | a bar widget and an anchored panel sent as `ui` trees; commands reachable from Views, Chrome, the CLI, a key, a menu and the launcher; permissions enforced by the broker. Inspired by Omarchy's `blackcode.power-menu` (MIT; nothing copied). |
| [`workspace-rail/`](workspace-rail/) | T0 built-in | the hero surface's manifest: required and optional compositor capabilities, the collapsed rail and its zone-less flyout, rename and move gated by capability |
| [`music-scratchpad/`](music-scratchpad/) | T1 declarative | a command with a declarative handler, a static bar button, a neutral key, and scroll Lua as a declared, reviewed permission |
| [`network-source/`](network-source/) | T0 built-in | `views-shell.network`, a source plugin: a producer that publishes a snapshot and commands and draws nothing |
| [`quick-settings-network/`](quick-settings-network/) | T1 declarative | `views-shell.qs-network`: a `tile` and a `page` entry bound to `views-shell.network/state`, cross-plugin calls under `call:` |
| [`quick-settings-bluetooth/`](quick-settings-bluetooth/) | T1 declarative | `views-shell.qs-bluetooth`: tile and device page; PIN prompts go to the core credential modal |
| [`quick-settings-audio/`](quick-settings-audio/) | T1 declarative | `views-shell.qs-audio`: two `slider` entries with a mute `iconAction`, and a routing page |
| [`quick-settings-brightness/`](quick-settings-brightness/) | T2 process | the third-party path into the same `slider` slot; the tree arrives at runtime |
| [`quick-settings-dnd/`](quick-settings-dnd/) | T1 declarative | `views-shell.qs-dnd`: one compact tile, one exact `call:` permission |
| [`quick-settings-media/`](quick-settings-media/) | T1 declarative | `views-shell.qs-media`: `card` entries built from the stock `mediaSession` node |
| [`quick-settings-power-footer/`](quick-settings-power-footer/) | T1 declarative | `views-shell.qs-power-footer`: the `footer` slot, an `open` handler, lock and the power menu |
| [`themes/`](themes/) | (theme, not a plugin) | Tom's themes as Omarchy theme directories: `noir` (the default), `claude-dark`, `claude-light`, and `all-black`, the Chrome theme he wears |

Every quick-settings item is a plugin. The first-party ones are written exactly as
a third party would write them, and the core has no private path for them.

The T2 examples' programs are complete JSON-RPC loops, but they have never
run against a views-shell process, because none exists yet. The scroll Lua file follows the
API shape in scroll's `TUTORIAL.md` and has not been run.
