# The `views-shell` command line

The third face of views-shell. Terminals, scripts and agents use it. It talks to the
running views-shell process over a Unix socket at `$XDG_RUNTIME_DIR/views-shell/views-shell.sock`
(JSON-RPC 2.0, one message per line). A few verbs work offline, without a running
views-shell: `plugin validate`, `plugin render`, `native-host --manifest`, `help` and
`version`.

## Conventions

- Every verb accepts `--json`, which prints one JSON document on stdout instead of
  text. Agents use `--json`.
- Exit codes: `0` success; `1` the command ran and failed; `2` usage error; `3` a
  permission or capability is missing; `69` views-shell is not running (`EX_UNAVAILABLE`).
- Help text and fish, bash and zsh completions are generated from the built-in
  verbs plus every enabled plugin's `contributes.cli`. Nobody maintains them by hand.
- Plugins never put binaries on PATH. Their verbs mount under `views-shell <name> <verb>`.
- Destructive verbs (`plugin remove`, and plugin verbs marked `confirm`) ask on a
  TTY and require `--yes` otherwise.

## Verbs

### Reaching commands

| Verb | Does |
|---|---|
| `views-shell call <plugin-id> <command> [--arg k=v]… [<json>]` | Invokes any command by id. This is what compositor bindings on niri and Hyprland, gestures and scripts use. Fully qualified ids also work as `views-shell call <plugin-id>/<command>`. |
| `views-shell <name> <verb> [args]` | A plugin's mounted verb, from `contributes.cli`. Example: `views-shell power-menu suspend`. |
| `views-shell commands [--plugin <id>]` | Lists registered commands, with enablement state. |

### Surfaces

| Verb | Does |
|---|---|
| `views-shell bar toggle|show|hide <bar-id>` | Controls exactly one bar. |
| `views-shell <surface> toggle|show|hide` | Every core surface has its own: `views-shell launcher`, `views-shell rail`, `views-shell quick-settings`, `views-shell notifications`, `views-shell cycle`, `views-shell cheatsheet`. |
| `views-shell gallery [--headless --capture <dir>]` | Opens the `views-shell/style` gallery on an overlay layer surface, or captures it for conformance. |
| `views-shell osd <kind> <value>` | Shows a value on the OSD without owning it. Example: the `brightness` script runs `views-shell osd brightness 0.42`. |
| `views-shell toast <text> [--icon <name>]` | A short system toast. |

### Chrome

| Verb | Does |
|---|---|
| `views-shell open [page]` | Opens or focuses a views-shell control page in Chrome. If the extension holds a native messaging port, views-shell pushes `open {page}` down it. Otherwise the CLI starts Chrome at `chrome-extension://<id>/options.html#<page>`, then focuses that Chrome window through the compositor adapter. Pages: `shortcuts` (default), `appearance`, `plugins`, `compositor`, `notifications`, `connection`. Bound globally, for example `bindsym $mod+Comma exec views-shell open`. |
| `views-shell native-host` | **Started by Chrome, never by hand.** The native messaging host for the views-shell extension. Reads Chrome's framing on stdin (a 32-bit native-order length, then UTF-8 JSON; at most 1 MB per message to Chrome), relays typed verbs to the command registry over the Unix socket, and writes pushes back. Exits when the port closes. Accepts only the verbs in the extension contract ([`../extension/README.md`](../extension/README.md)); never a raw compositor command or Lua. |
| `views-shell native-host --manifest` | Prints the host manifest (`name`, `description`, `path`, `"type": "stdio"`, `allowed_origins` with the pinned extension id) for Home Manager to write under `~/.config/google-chrome/NativeMessagingHosts/` and `~/.config/chromium/NativeMessagingHosts/`. Offline. |

### Plugins

The lifecycle follows Omarchy's verbs. One validator is shared by the loader and
`validate`.

| Verb | Does |
|---|---|
| `views-shell plugin list [--json]` | Discovered plugins with tier, state, version and pinned commit. |
| `views-shell plugin add <git-url|path> [--rev <commit>] [--enable]` | Warns that process plugins are unsandboxed apart from the broker, validates without running plugin code, refuses a taken id, and lands the plugin **disabled** unless `--enable`. Pins the given or reviewed commit. Never runs hooks or sudo. |
| `views-shell plugin enable <id>` | Shows the declared permissions and capabilities, runs `check`, and refuses on a key conflict or a missing required capability. Writes the slot files, then applies. |
| `views-shell plugin disable <id>` | Deletes the plugin's slot files, then applies. |
| `views-shell plugin update [<id>]` | Fetches, shows the diff, moves fast-forward only to a newer pinned commit, and rolls back if validation fails. |
| `views-shell plugin remove <id>` | Disables, then deletes a git checkout, unlinks a symlink, or moves a hand-made directory to a timestamped backup. |
| `views-shell plugin clone <views-shell.x>` | Copies a built-in's manifest and declarative parts to `<user>.x`, routes calls for the old id to the clone, and keeps its placement and settings. |
| `views-shell plugin validate <dir>` | Validates a plugin directory against the schemas. Offline. |
| `views-shell plugin check [<id>]` | Finds key conflicts across every slot fragment and against the compositor's live bindings (`GET_BINDINGS` on scroll). |
| `views-shell plugin render --compositor <c> --out <dir> [--enabled <file>]` | Produces the slot files from a declared enabled set, without a running views-shell. Used at Nix build time when the config is read-only. Offline. |

### Configuration and state

| Verb | Does |
|---|---|
| `views-shell config get|set <plugin-id>.<key> [value]` | Reads or writes `views-shell.json` through the views-shell process, which refuses when the file is Nix-managed and read-only and prints the path to edit instead. |
| `views-shell theme apply [<name>]` | Makes `<name>` (an Omarchy theme directory) the active theme, or re-reads the active one: rebuilds the colour mixers and redraws every surface live, and writes the request file the Chrome policy writer watches. |
| `views-shell theme list` | The theme directories views-shell can see. |
| `views-shell compositor` | The active adapter, its version and its capability set. |
| `views-shell wm [--json]` | The `WmModel`: outputs, workspaces, windows, focus. |
| `views-shell service status|logs` | The views-shell user service. |
| `views-shell version` | The views-shell commit (no release numbers before v1), plugin-protocol version, Chromium base. |

## What the CLI never does

- It never exposes raw compositor commands or Lua evaluation.
- It never unlocks. `lock` is a verb; `unlock` is not one anywhere.
- It never writes the user's own config files. It writes slot files only, and only
  through the views-shell process (or `render --out` into a directory the caller names).
