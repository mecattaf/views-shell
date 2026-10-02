# Writing a views-shell plugin

A plugin is a views-shell shell module: a directory with a
`views-shell-plugin.json` manifest, optional `ui/*.json` trees, and, for a process
plugin, a program. (The word *extension* means only the Chrome extension; see
[`naming.md`](naming.md).) This page is enough to write one from the repository
alone. Every claim in it is checked by a tool you can run:

| Tool | What it tells you |
|---|---|
| `tools/validate.sh` | the manifest and every tree validate, every example's registry view and render trace match their fixtures, the T2 programs pass the conformance runner, the slot files match `cli/testdata` |
| `tools/plugin-registry.py <dir>` | what views-shell will register from your manifest, without running any plugin code |
| `tools/ui-tree-render.py <ui.json> --snapshot <s>` | which stock Views class each node of your tree becomes, with every binding resolved |
| `tools/plugin-conformance.py <dir>` | plays views-shell against your T2 program over stdio and judges every protocol row |
| `cli/views-shell plugin render --compositor <c> --out <dir> --plugin <dir>` | the compositor slot file your keybindings become |

The examples in [`../examples/`](../examples/) are the worked cases; each README
names its tier, contributions, capabilities, permissions and registry fixture.
None of them has run against a views-shell process yet: the C++ plugin host is
being written against exactly these tools.

## 1. The tiers (rule R11)

Plugins supply data and handlers, never pixels. There are four tiers; the
manifest's `runtime.mode` picks one of the first three.

| Tier | `runtime.mode` | May contain | May not contain |
|---|---|---|---|
| T0 built-in | `builtin`, with `runtime.target` (a `//views_shell/...` GN label) | first-party C++ in the views-shell tree; it is the only tier that links Views. Source plugins (producers over NetworkManager, BlueZ, PipeWire, UPower, MPRIS…) are T0 and draw nothing | an id outside `views-shell.*`; command `handler`s; `runtime.exec` |
| T1 declarative | `declarative` | a manifest, `ui/*.json` trees and declared files (scroll Lua). Commands carry a `handler` of one of four kinds: `{compositor: <capability>, args}`, `{exec: <program>, args}`, `{call: <plugin/command>, args}`, `{open: <control page>}` | code of any kind; `sources` (it runs nothing that could publish one); `exec` or `target` in `runtime` |
| T2 process | `process`, with `runtime.exec` (relative to the plugin directory) and `engines.protocol: 1` | a program in any language speaking JSON-RPC 2.0 on stdio; it sends `ui` trees and full snapshots at runtime, and asks the broker for everything else | a `views-shell.*` id; command `handler`s; `ui` files on quick-settings entries (the tree arrives with `surface/setTree`); a binary on PATH |
| T3 | (not accepted yet) | a JavaScript host, reserved for later | |

Every quick-settings item is a plugin. The first-party entries
(`examples/quick-settings-*`) are T1, written exactly as a third party would
write them, and bind to T0 source plugins that draw nothing. A third party
without a first-party source writes T2, as `examples/quick-settings-brightness`
does. Third-party plugins carry no C++.

## 2. The manifest, field by field

The schema is [`../schemas/views-shell-plugin.schema.json`](../schemas/views-shell-plugin.schema.json).
`tools/validate.py` checks it plus the cross-file rules below.

| Field | Rule | Why |
|---|---|---|
| `$schema`, `schemaVersion: 1` | the schema path and version | editors validate as you type |
| `id` | `publisher.name`, lower case, at least one dot; permanent, never reused | the id qualifies every command (`<id>/<command>`), names the slot file and the settings key. `views-shell.*` is **reserved**: only built-in (T0) and declarative (T1) first-party plugins may use it, and built-in mode is only for `views-shell.*`. A process plugin can never pose as a first-party one |
| `engines.views-shell` | a version range, **never a wildcard** (`*` is rejected) | a plugin states what it was tested against; a wildcard is a promise nobody checked |
| `engines.protocol` | required for `process`; `1` today | the host refuses a plugin whose `initialize` answer names another version |
| `runtime` | `mode`, plus `target` (T0) or `exec` and `args` (T2), never both | one way to run per tier |
| `permissions` | the **only way out** of the plugin: `exec:<program>`, `path:read:`/`path:write:<path>`, `network[:<host>]`, `notifications`, `media`, `clipboard:read`/`write`, `dbus:session:`/`dbus:system:<name>`, `compositor:<capability>`, `scroll.lua`, `scroll.lua.eval`, `call:<plugin>/<command>` or `call:<plugin>/*`, `state:read:<plugin>/<source>` | shown at enable, enforced by the broker for T2. An undeclared request is answered `-32001` and nothing runs. A tree or handler that calls another plugin's command without a covering `call:` fails validation |
| `requires.compositor.required` / `.optional` | **capabilities, never compositor names** (rule R14): `workspaces.rename`, `scratchpad.toggle`, `session.exit`… (see [`compositor-adapters.md`](compositor-adapters.md)) | the same plugin works on scroll, sway, niri and Hyprland. A missing required capability means the plugin is not registered; an optional one gates a command through `enablement` or a `when` |
| `requires.plugins` | the plugins whose sources or commands you use | the registry view lists them as `dependencies`; disabling a source removes the domain (rule R10) |
| `requires.helpers` | `{bin, nixpkgs, optional?}` for programs the broker will run | the dotfiles can install them; the plugin never installs anything |
| `contributes.commands` | `id`, `title`, optional `icon`, `category`, `enablement` (a `when` expression), `args` (`{name, type, required}`), `confirm`, `result` (`none`, `text`, `json`), and for T1 a `handler`; `surface` + `verb` make a surface verb (`toggle`, `show`, `hide`) that views-shell handles itself | one command id is reachable from every face: Views, Chrome, the CLI, a key, a menu, the launcher |
| `contributes.surfaces` | `id`, `kind` (`bar`, `bar-widget`, `panel`, `overlay`, `menu`, `service`, `picker-provider`), `keyboard` (`none`, `on-demand` by default, `exclusive`), `anchor` (`surface:<id>`), `defaultSection`, `allowMultiple`, `ui` (T0/T1), `prefix` (pickers), `when` | every surface is a layer surface or its popup (rule R1); the plugin names the kind, views-shell owns the geometry. Say `keyboard: none` on a bar widget: the default lets a click take keyboard focus |
| `contributes.quickSettings` | `id`, `slot` (`tile`, `slider`, `card`, `footer`, `page`), `ui` (T0/T1 only), `state` (`<plugin>/<source>`), `size` (`primary` or `compact`, tiles), `page` (a declared `page` entry), `title`, `order`, `when` | the host owns open, close, focus, Esc, the page stack and ordering; an entry only fills its slot |
| `contributes.keybindings` | `command`, and `key` (neutral: `Mod+Shift+Escape`) or `keyFrom: config.<property>`; `args`, `when`, `release`, `locked`, `mode` | views-shell renders them per compositor into slot files (§6) |
| `contributes.menus`, `.launcher`, `.cli` | rows that name commands: a menu `location` (`views-shell.menu/...`, `rail/workspace`, `rail/window`, `bar/context`, `tray/context`), launcher rows with `keywords`, CLI verbs mounted under `views-shell <name> <verb>` | these add no behaviour, only reach; plugins never put binaries on PATH |
| `contributes.configuration` | `title` and `properties` (JSON Schema per property, plus `scope`: `global` or `instance`) | the plugin receives its full configuration with defaults filled in, in `initialize` and `config/changed` |
| `contributes.events` | compositor events to receive (`workspace`, `window`, `output`…) | events go only to the names declared |
| `contributes.sources` | T0 (and T2) only: `{id, description, schema}` published as `<plugin>/<id>` | full snapshots other plugins' trees bind to |
| `contributes.compositor.scroll.lua` | Lua files run by a `lua` line in the slot file | needs the `scroll.lua` capability **and** permission (rule R13) |

**Activation is inferred, never declared.** There is no `activationEvents`
field. The registry derives it from what you contribute (the full list is in
`tools/plugin-registry.py`'s docstring): a bar, bar-widget or service surface
gives `onStartup`; a panel, overlay or menu gives `onSurface:<id>`; every command
gives `onCommand:<plugin/cmd>`; every resolved key `onKeybinding:<key>`; sources
`onSource:`, events `onEvent:`, quick-settings entries `onQuickSettings:`. A
process nobody needs is never started.

### What views-shell will register

`tools/plugin-registry.py examples/music-scratchpad` prints the registry view,
the exact record the C++ plugin host builds (committed as
`tools/fixtures/registry/example.music-scratchpad.json`):

```json
  "activation": [
    "onCommand:example.music-scratchpad/toggle",
    "onKeybinding:F9",
    "onStartup"
  ],
  "capabilities": { "optional": ["scroll.lua"], "required": ["scratchpad.toggle"] },
```

Every absent optional field is `null`, every default is filled in
(`keyboard`, `confirm`, `result`, `order`, `scope`…), and every command reference
is qualified. Read the view after each manifest edit: it is where a manifest that
says one thing and means another shows. Chapter 2's reconciliation found four
such cases in the examples (a bar widget that defaulted to taking keyboard focus,
a `confirm` setting no code read, an `open` handler naming a page that does not
exist, a `when` over a plugin that does not exist); each README records its fix.
After a deliberate change, regenerate the fixture with
`tools/plugin-registry.py <dir> --write` and review its diff in the same commit.
A manifest the validator rejects prints `REJECT <file> <reason>` and is never
registered.

## 3. The ui tree in practice

T1 trees live in `ui/*.json`; T2 programs send the same JSON with
`surface/setTree`. The schema is [`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json);
how each node becomes a stock Views widget is
[`../schemas/ui-tree-rendering.md`](../schemas/ui-tree-rendering.md).

- **Containers:** `column` and `row` (`views::BoxLayoutView`), `stack`
  (`FillLayout`), `grid` (`views::TableLayoutView`), `scroll`, and `repeat`,
  which expands its `template` once per item of a bound array.
- **Content and controls:** `label`, `icon`, `image`, `badge`, `dot`,
  `separator`, `spacer`, `progress`, `button`, `iconButton`, `switch`,
  `checkbox`, `radioGroup`, `select`, `slider`, `textfield`, `listItem`.
- **Bindings:** any value may be `{"$bind": "/pointer"}`, an RFC 6901 pointer into
  your latest snapshot; inside a `repeat`, `./a` reads the current item; with
  `"source": "<plugin>/<source>"` it reads another plugin's source instead.
- **Formats:** `text`, `percent` (1 = 100 %), `duration` (seconds), `bytes`
  (base 1024), `time` and `relative-time`. Format in the tree, not in the
  snapshot, so locale and clock stay views-shell's.
- **Actions:** `action` (and `iconAction` on a slider) is
  `{command, args, confirm?, close?}`. A bare command is yours; a qualified
  `<plugin>/<command>` needs a `call:` permission. `close: true` closes the
  surface after the call.
- **Look:** colour is a semantic `role` (`default`, `subtle`, `primary`,
  `positive`, `warning`, `alert`, `disabled`) and text a `typography` token
  (`display`, `title`, `headline`, `body`, `body-strong`, `button`,
  `annotation`, `label`); spacing and sizes are enums (`tight`, `normal`,
  `loose`; `small`, `medium`, `large`). There are no colour literals, no pixel
  sizes and no custom painting. The theme decides what a role looks like
  (rule R17).

### Not drawn in chapter 2

Five node types validate and appear in the render trace with their resolved
props, but have no view yet: the trace gives them `view: null` and
`error: "not drawn in chapter 2"` (the view table in
[`../schemas/ui-tree-rendering.md`](../schemas/ui-tree-rendering.md)):

| Node | Will be drawn by | Used by |
|---|---|---|
| `tile` | the FeatureTile port (A032) | the network, Bluetooth and do-not-disturb tiles |
| `markdown` | not chosen yet | no example |
| `tabSlider` | not chosen yet | no example |
| `keyChips` | the key-chips port in the style kit ([`architecture.md`](architecture.md) §4) | the power menu's shortcut column |
| `mediaSession` | the stock `global_media_controls::MediaItemUIView` ([`architecture.md`](architecture.md) §7) | the media cards |

Write them anyway if you need them; the trace already says what the face will
receive. Check your tree with
`tools/ui-tree-render.py <ui.json> --snapshot <snapshot.json>`.

## 4. The T2 session, from initialize to shutdown

The protocol is [`../schemas/plugin-protocol.md`](../schemas/plugin-protocol.md),
including the table the conformance runner settled. One JSON-RPC 2.0 object per
line on stdin and stdout, nothing else on stdout, diagnostics on stderr. These
are the real lines of one conformance run against
[`../examples/echo-process/`](../examples/echo-process/), captured by a pass-through
wrapper between the runner and `plugin.py` (`->` host to plugin, `<-` plugin to
host; the two long `setTree` lines are shortened at `…`):

1. The host starts the program and asks first. The plugin sends nothing before
   it answers, and answers exactly `{protocol: 1}`:
   ```
   -> {"jsonrpc":"2.0","id":"host-1","method":"initialize","params":{"protocol":1,"shellVersion":"0.1.0","pluginId":"example.echo","capabilities":[],"config":{"greeting":"hello"},"locale":"en-US"}}
   <- {"jsonrpc":"2.0","id":"host-1","result":{"protocol":1}}
   ```
2. Within 2 s: one `surface/setTree` per contributed surface and at least one
   full `snapshot`, all notifications:
   ```
   <- {"jsonrpc":"2.0","method":"surface/setTree","params":{"surface":"button","tree":{"schemaVersion":1,"root":{"type":"button","label":{"$bind":"/counter","format":"text"},"icon":"add",…}}}}
   <- {"jsonrpc":"2.0","method":"surface/setTree","params":{"surface":"panel","tree":{"schemaVersion":1,"root":{"type":"column","container":"rounded","spacing":"normal","children":[…]}}}}
   <- {"jsonrpc":"2.0","method":"snapshot","params":{"data":{"counter":0,"next":1,"greeting":"hello","lastEvent":{"name":"","data":null}}}}
   ```
3. Surfaces open and close; each answers `{}`:
   ```
   -> {"jsonrpc":"2.0","id":"host-2","method":"surface/opened","params":{"surface":"button","instance":"conformance-1"}}
   <- {"jsonrpc":"2.0","id":"host-2","result":{}}
   ```
4. A command. Both sides are full-duplex: `ping` asks the broker to `notify`
   (declared as `notifications`) before its own answer arrives:
   ```
   -> {"jsonrpc":"2.0","id":"host-6","method":"command/invoke","params":{"command":"ping","args":{"text":"conformance"},"source":"cli"}}
   <- {"jsonrpc":"2.0","id":"echo-1","method":"notify","params":{"summary":"Echo","body":"conformance"}}
   <- {"jsonrpc":"2.0","id":"host-6","result":{"result":{"args":{"text":"conformance"}}}}
   -> {"jsonrpc":"2.0","id":"echo-1","result":{}}
   ```
5. A state change is answered, then followed by a whole new snapshot, never a
   patch:
   ```
   -> {"jsonrpc":"2.0","id":"host-7","method":"command/invoke","params":{"command":"set-counter","args":{"value":7},"source":"cli"}}
   <- {"jsonrpc":"2.0","id":"host-7","result":{}}
   <- {"jsonrpc":"2.0","method":"snapshot","params":{"data":{"counter":7,"next":8,"greeting":"hello","lastEvent":{"name":"","data":null}}}}
   ```
6. Permissions are the only way out. `try-exec` asks for a program the manifest
   never declared; the broker refuses with `-32001`, runs nothing, and the plugin
   carries on:
   ```
   -> {"jsonrpc":"2.0","id":"host-8","method":"command/invoke","params":{"command":"try-exec","args":{},"source":"cli"}}
   <- {"jsonrpc":"2.0","id":"echo-2","method":"exec","params":{"program":"true","args":[]}}
   -> {"jsonrpc":"2.0","id":"echo-2","error":{"code":-32001,"message":"permission not declared"}}
   <- {"jsonrpc":"2.0","id":"host-8","result":{"result":{"program":"true","answer":{"error":{"code":-32001,"message":"permission not declared"}}}}}
   ```
7. An undeclared command is invalid params (`-32602`):
   ```
   -> {"jsonrpc":"2.0","id":"host-9","method":"command/invoke","params":{"command":"conformance-undeclared","args":{},"source":"cli"}}
   <- {"jsonrpc":"2.0","id":"host-9","error":{"code":-32602,"message":"unknown command 'conformance-undeclared'"}}
   ```
8. Configuration arrives whole, defaults filled in:
   ```
   -> {"jsonrpc":"2.0","id":"host-10","method":"config/changed","params":{"config":{"greeting":"hello (conformance)"}}}
   <- {"jsonrpc":"2.0","id":"host-10","result":{}}
   <- {"jsonrpc":"2.0","method":"snapshot","params":{"data":{"counter":7,"next":8,"greeting":"hello (conformance)","lastEvent":{"name":"","data":null}}}}
   ```
9. An event (a notification, only for names in `contributes.events`), and an
   unknown request (`-32601`); answers may come in any order:
   ```
   -> {"jsonrpc":"2.0","method":"event","params":{"name":"workspace","data":{"conformance":true}}}
   -> {"jsonrpc":"2.0","id":"host-11","method":"conformance/unknown-method","params":{}}
   <- {"jsonrpc":"2.0","method":"snapshot","params":{"data":{"counter":7,"next":8,"greeting":"hello (conformance)","lastEvent":{"name":"workspace","data":{"conformance":true}}}}}
   <- {"jsonrpc":"2.0","id":"host-11","error":{"code":-32601,"message":"method not found: conformance/unknown-method"}}
   ```
10. Malformed input is dropped without an answer, and the plugin still answers
    the next request:
    ```
    -> {this is not json
    -> [1,2]
    -> {"jsonrpc":"2.0","id":"host-12","method":"conformance/unknown-method","params":{}}
    <- {"jsonrpc":"2.0","id":"host-12","error":{"code":-32601,"message":"method not found: conformance/unknown-method"}}
    ```
11. Shutdown answers `{}`; the host closes stdin and the process exits 0 within
    1 s:
    ```
    -> {"jsonrpc":"2.0","id":"host-15","method":"shutdown","params":{}}
    <- {"jsonrpc":"2.0","id":"host-15","result":{}}
    ```

## 5. Running the conformance runner

The runner needs a python3 with `jsonschema`; `tools/validate.sh` fetches one
from nixpkgs when the local python lacks it. By hand, from the repository root:

```sh
nix shell nixpkgs#python3Packages.jsonschema -c python3 tools/plugin-conformance.py examples/echo-process
```

Its output on 2026-10-02 (exit 0):

```
ok   example.echo manifest (tools/validate.py checks)
ok   example.echo runtime.mode process, engines.protocol 1
ok   example.echo initialize answers {protocol: 1}
ok   example.echo startup: a snapshot and 2 surface/setTree within 2 s
ok   example.echo setTree names only contributed surfaces
ok   example.echo tree button: ui-tree schema and commands
ok   example.echo tree panel: ui-tree schema and commands
ok   example.echo tree button: bindings resolve in the startup snapshot
ok   example.echo tree panel: bindings resolve in the startup snapshot
ok   example.echo surface/opened button answers {}
ok   example.echo surface/closed button answers {}
ok   example.echo surface/opened panel answers {}
ok   example.echo surface/closed panel answers {}
ok   example.echo command/invoke ping answers a json result (asked the broker: notify notifications)
ok   example.echo command/invoke set-counter answers a none result
ok   example.echo command/invoke try-exec answers a json result (asked the broker: exec exec:true -> -32001)
ok   example.echo command/invoke of an undeclared command answers -32602
ok   example.echo config/changed (1 properties changed) answers {}
ok   example.echo event workspace delivered, plugin alive
ok   example.echo malformed lines (not JSON; JSON but not an object) dropped, plugin alive
ok   example.echo unknown notification ignored, unknown request answers -32601
ok   example.echo broker refused exec:true with -32001, plugin alive
ok   example.echo tree button: bindings resolve in the final snapshot
ok   example.echo tree panel: bindings resolve in the final snapshot
ok   example.echo shutdown answers {}
ok   example.echo process exits 0 within 1 s of shutdown
ok   example.echo stream: 23 messages, all JSON-RPC 2.0 protocol traffic
CONFORMANCE-OK example.echo
```

The runner never executes what a plugin asks for: declared requests get a canned
success, undeclared ones `-32001`. It invokes each declared command without a
`verb` once with sample arguments (`"conformance"`, `7`, `0.5`, `true`), so a
command must accept any valid value of its declared type. It does not yet check
plugin-specific effects, `picker/query`, `source/changed`, or the end-of-input
shutdown path; handle them per the protocol anyway.

## 6. Slot files: how a keybinding reaches the compositor

A keybinding is declared once, compositor-neutrally. views-shell renders it into a
**slot file** per compositor that the user's config includes once; it never
writes the user's own config ([`../schemas/keybinding-rendering.md`](../schemas/keybinding-rendering.md)).
The goldens in [`../cli/testdata/`](../cli/testdata/) are rendered from the
examples by `cli/views-shell plugin render` and compared by `tools/validate.sh`.
For `example.power-menu` on scroll
(`cli/testdata/scroll/example.power-menu.conf`):

```
# Generated by views-shell for example.power-menu 0.1.0. Do not edit: changes are
# overwritten. Disable the plugin or override the key in your own config.
bindsym $mod+Shift+Escape nop views-shell example.power-menu toggle
bindsym $mod+Ctrl+l nop views-shell example.power-menu lock
```

The first line comes from `keyFrom: config.hotkey`, resolved to the property's
default; a user's setting overrides it at run time. On scroll and sway the
binding is a `nop`: the compositor keeps the key (so `--locked`, `--release` and
modes work), does nothing itself, and emits a `binding` event. The scroll adapter
(`shell/wm/adapters/scroll/`) hands every binding whose command starts with
`nop views-shell ` to its delegate (`OnBinding`) with the rest of the command;
the command registry, once it exists, resolves `example.power-menu toggle` to
`example.power-menu/toggle` and invokes it with `source: "keybinding"`.
Nothing forks per keypress, and the binding is harmless when views-shell is not
running. niri and Hyprland have no binding event, so their slot files spawn
`views-shell call <plugin> <command>` instead (`cli/testdata/niri/views-shell.kdl`,
`cli/testdata/hyprland/`). A plugin that ships scroll Lua gets a `lua` line in the
same scroll slot file, so the script re-runs after every reload
(`cli/testdata/scroll/example.music-scratchpad.conf`). A key the user binds by
hand wins, and `views-shell plugin check` blocks enabling a plugin whose key
conflicts.

## 7. What never works

These are rules, not gaps. A manifest or tree that tries one fails validation, is
refused by the broker, or has no field to say it in.

- **Pixels, sizes and colours (R11, R17).** A plugin sends data and trees. There
  is no colour literal, no pixel size, no custom paint and no `views::View` to
  receive; the `image` node shows a file and is never used to paint chrome.
- **Raw compositor commands (R14, R16).** There is no "run this sway command".
  A plugin asks for a typed capability (`compositor/command` with
  `compositor:<capability>`, or a T1 `{compositor: <capability>}` handler), and
  the adapter spells it for the compositor in use.
- **Lua from Chrome (R13, R16).** Shipping scroll Lua is the declared
  `scroll.lua` capability and permission, shown at enable. Runtime `LUA_EVAL` is
  `scroll.lua.eval`, and it is never reachable from Chrome, the extension or the
  native messaging host.
- **Unlock (R4).** `lock` is a verb; `unlock` is not a method, a command or a CLI
  verb anywhere.
- **Password nodes and credentials (R25).** A `ui` tree has no password node;
  `textfield` has no obscured mode. NetworkManager secrets, BlueZ PINs and polkit
  prompts go to the core credential modal, and credentials never pass through a
  plugin.
- **A first-party id or C++ for a third party (R11).** `views-shell.*` ids are
  reserved; `builtin` mode is only for them; a third-party plugin is T1 or T2.
- **Patches instead of snapshots (R10).** `snapshot`, `source/snapshot` and
  `source/changed` always carry the whole state.
- **A wildcard engine.** `engines.views-shell: "*"` is rejected.

## 8. A checklist before you publish

1. `tools/validate.py` is clean for your directory (copy it under `examples/` to
   use `tools/validate.sh` as is).
2. `tools/plugin-registry.py <dir>` shows the activation, capabilities and
   permissions you meant, with no surprising default.
3. Each tree renders with `tools/ui-tree-render.py` against a snapshot you
   wrote, with no `$unresolved`.
4. A T2 program prints `CONFORMANCE-OK <id>` from `tools/plugin-conformance.py`.
5. `cli/views-shell plugin render --compositor scroll --out <tmp> --plugin <dir>`
   writes the keys you expect.
