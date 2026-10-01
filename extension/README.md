# The views-shell Chrome extension

One Manifest V3 extension for stock Google Chrome. It is the second face of views-shell:
the control pages open in ordinary tabs, the way Claude in Chrome opens its options
page. Think of it as a full-tab `chrome://settings` for your desktop, or DevTools
for your window manager.

The word **extension** in views-shell means only this. Shell modules are **plugins**.

## What it has

| Piece | Where | State |
|---|---|---|
| Options page in a tab (`options_ui.open_in_tab`) | `options.html`, `options.js` | skeleton: the six pages and the shortcuts table; shows "not installed" or "not running" until views-shell exists |
| New-tab override in the active theme | `newtab.html`, `newtab.js` | skeleton: a workspace glance when views-shell answers, an empty page in the system scheme otherwise |
| Omnibox keyword `views-shell` | `background.js` | pages, and `views-shell <plugin-id>/<command>` through a typed verb |
| In-Chrome command, suggested `Ctrl+Shift+K` (PROPOSED; `Ctrl+Shift+Y` is the alternate) | `manifest.json` `commands` | opens or focuses the views-shell tab |
| Native messaging client | `lib/host.js` | one `connectNative` port; request/response by id, pushes without one |

What it never has: a side panel, content scripts, an overlay on the page you are
reading, a literal `views-shell://` scheme, a theme of its own, or a fork of Chrome.
`Ctrl+Shift+I` can be bound to the command only by hand in
`chrome://extensions/shortcuts`, at the cost of the DevTools key. The global key is
a compositor binding (`bindsym $mod+Comma exec views-shell open`), which works whether or
not Chrome has focus.

## Load it unpacked (development)

`chrome://extensions` → Developer mode → Load unpacked → this directory. Google
Chrome does not accept `--load-extension`. **The extension id changes with the
directory path until a `key` is pinned** (open decision P12). Native messaging's
`allowed_origins` takes an exact id, and `chrome.storage.sync` belongs to the id,
so the key must be pinned before the host manifest is declared in the dotfiles and
before the repository directory is ever renamed.

## The native messaging contract

The pages talk to views-shell through Chrome native messaging, not a network service
(rule R16). This replaces the localhost service of earlier drafts: no port, no
`Origin` and `Host` checks, no bearer tokens, no pairing, no CORS, no DNS-rebinding
defence. It is the pattern Claude in Chrome and Claude Code already use on Tom's
machine.

**The host.** `views-shell native-host`, the CLI in a mode. Chrome starts it per
`chrome.runtime.connectNative` port and stops it when the port closes. Its
manifest, printed by `views-shell native-host --manifest` and written by Home Manager:

```json
{
  "name": "views-shell.host",
  "description": "views-shell desktop shell",
  "path": "/etc/profiles/per-user/tom/bin/views-shell-native-host",
  "type": "stdio",
  "allowed_origins": ["chrome-extension://<pinned extension id>/"]
}
```

at `~/.config/google-chrome/NativeMessagingHosts/views-shell.host.json` (Chromium:
`~/.config/chromium/NativeMessagingHosts/`). `path` must be absolute; it is a
two-line wrapper that execs `views-shell native-host`, as Claude Code's host does. The
host name is a placeholder and is renamed with the product.

**Framing.** Chrome's: a 32-bit native-order length, then UTF-8 JSON. At most 1 MB
per message from the host. The host relays to the views-shell process over
`$XDG_RUNTIME_DIR/views-shell/views-shell.sock`, the same socket the CLI uses.

**Messages.** A request is `{id, verb, params}`; its answer is `{id, result}` or
`{id, error: {code, message}}`, with `code` `not-running` when the views-shell process is
not up. Anything without an `id` is a push. Only typed verbs exist: there is no
verb for a raw compositor command or for Lua evaluation, and `unlock` is not a
verb.

| Verb | Does |
|---|---|
| `status` | `{machine, compositor, capabilities, theme: {name, mode, background, foreground, light_foreground, muted, accent, red}}` |
| `bindings.list` | Resolved bindings: `{key, command, plugin?, locked, file?, line?}`. Locked rows come from the user's own config. |
| `bindings.set` `{plugin, index, key}` | Edits a slot binding. Written to the slot file first, then applied live or by reload. Refused for locked rows. |
| `plugins.list` | Plugins, state, permissions, configuration schema and values. |
| `plugins.enable` · `plugins.disable` `{plugin}` | Same checks as the CLI: permissions shown, conflicts block enable. |
| `plugins.config` `{plugin, values}` | Writes `views-shell.json`. Refused with `{code: "read-only", path}` when the config is Nix-managed; the page then shows the path to edit. |
| `commands.list` | Registered commands with titles and enablement. |
| `commands.invoke` `{plugin, command, args, source: "chrome"}` | Invokes a command. |
| `wm` | The `WmModel` snapshot. |

| Push | When |
|---|---|
| `{type: "bindings-changed"}` | a slot file changed |
| `{type: "reload"}` | relayed from the compositor |
| `{type: "config-error", ...}` | the compositor rejected a reload |
| `{type: "wm", ...}` | the window model changed (only while a page asked for it) |
| `{type: "theme", theme}` | `views-shell theme apply` ran |
| `{type: "open", page}` | `views-shell open` ran |

An open port keeps the extension's service worker alive (Chrome 105 and later), so
pushes need no polling and no reconnect timer.

**`views-shell open`.** If a port is open, views-shell pushes `open`. If none is, the CLI starts
Chrome at `chrome-extension://<id>/options.html#<page>`, then focuses that Chrome
window through the compositor adapter.

## Where configuration lives

| Layer | Holds | Written by | Without a Google account |
|---|---|---|---|
| Dotfiles (`views-shell.json`, slot files, theme directories) | the real configuration, the ground truth | Tom by hand; views-shell writes only its slot files; Nix-managed files are read-only and the page shows the path | everything works |
| `chrome.storage.sync` | light references only: machine names, keymap profile, page preferences, the dotfiles repository URL and ref, the theme name | the extension | behaves as local storage; nothing is lost |
| Google Drive `appDataFolder` | **not in v1** | — | — |

`chrome.storage.sync` quotas, from Chrome's documentation: 102,400 bytes in total,
8,192 bytes per item, 512 items, 120 writes per minute and 1,800 per hour. Chrome
keeps the data locally while offline and resumes syncing later; with sync off it
is local storage. Drive's `appDataFolder` is a hidden per-app folder the user
cannot see, needing the `drive.appdata` scope and an OAuth client in a Google Cloud
project. A hidden copy works against "dotfiles are ground truth", and the dotfiles
repository is already the backup, so it is left out (open decision P22).
