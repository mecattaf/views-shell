# The T2 plugin protocol

A process plugin (`runtime.mode: "process"`) is a child process that views-shell starts
and supervises. It speaks JSON-RPC 2.0 on stdin and stdout, one message per line
(newline-delimited JSON). stderr goes to the journal. Protocol version: **1**
(`engines.protocol`).

Each plugin runs in its own `systemd-run --user --scope` unit, so a failure is
visible to the fleet's unit-failure tripwire. A crash restarts it with backoff
and shows a badge on its surfaces. A plugin process that nobody needs is never
started (activation is inferred from contributions).

## Lifecycle

| Direction | Method | Params | Result |
|---|---|---|---|
| views-shell → plugin | `initialize` | `{protocol, shellVersion, pluginId, capabilities: [..], config: {..}, locale}` | `{protocol}` |
| views-shell → plugin | `shutdown` | `{}` | `{}`; the process exits within its grace period or is killed |

## From views-shell to the plugin

| Method | Params | Result |
|---|---|---|
| `command/invoke` | `{command, args, source: "views"\|"chrome"\|"cli"\|"keybinding"\|"menu"\|"launcher", instance?}` | `{result?}` matching the command's `result` type |
| `surface/opened` | `{surface, instance}` | `{}` |
| `surface/closed` | `{surface, instance}` | `{}` |
| `config/changed` | `{config}` (the full plugin config) | `{}` |
| `event` | `{name, data}` for each subscribed `contributes.events` entry | notification, no result |
| `picker/query` | `{surface, query}` | `{items: [{id, label, sublabel?, icon?, action}]}` |
| `source/changed` (notification) | `{source, data}` | Delivered for each source the plugin holds `state:read:<plugin-id>/<source>` on. Always a full snapshot. |

## From the plugin to views-shell

| Method | Params | Notes |
|---|---|---|
| `snapshot` (notification) | `{data}` | The full state the plugin's `ui` trees bind to. Always a full snapshot, never a patch. views-shell diffs it. |
| `surface/setTree` (notification) | `{surface, tree}` | A `ui` tree (`schemas/ui-tree.schema.json`). Sent when the structure changes; values flow through `snapshot`. For a quick-settings entry, `surface` is the entry id. |
| `source/snapshot` (notification) | `{source, data}` | Publishes one of the plugin's declared `contributes.sources`, for other plugins' trees to bind to. Always a full snapshot. |
| `notify` | `{summary, body?, icon?, urgency?}` | Requires the `notifications` permission. |
| `toast` | `{text, icon?}` | A short system toast. |
| `exec` | `{program, args}` | Requires `exec:<program>`. The broker runs it and returns `{exitCode, stdout, stderr}`. |
| `compositor/command` | `{capability, args}` | Typed compositor verbs only, gated by `compositor:<capability>`. Never raw IPC. |

## Rules

- A plugin never sends pixels, sizes or colours. It sends data and trees.
- Every request from the plugin is checked against its declared permissions. An
  undeclared call fails with JSON-RPC error `-32001` ("permission not declared").
- Credentials never pass through a plugin. Polkit, NetworkManager and BlueZ prompts
  belong to views-shell's own credential modal.
- `unlock` is not a method anywhere.

## Settled by the conformance runner (w1d, 2026-10-02)

`tools/plugin-conformance.py <plugin-dir>` plays views-shell against one process
plugin and is the arbiter where the tables above were silent. It adds no method.
`examples/echo-process/` is the reference plugin that passes every row;
`tools/validate.sh` runs the runner against the three T2 examples.

| Question | Settled | The runner checks |
|---|---|---|
| Framing | One JSON-RPC 2.0 object per line, UTF-8, `\n`-terminated. Empty lines are ignored. A plugin's stdout carries nothing else; diagnostics go to stderr. | Every non-empty stdout line parses as an object with `"jsonrpc": "2.0"`. |
| Malformed input | A receiver drops a line that is not JSON, or is JSON but not an object, logs it on stderr and keeps going. It sends no `-32700` (a reply with `id: null` names no request on a line-framed pipe). | After `{this is not json` and `[1,2]` the plugin still answers the probe. |
| Before `initialize` | The plugin sends nothing until it has answered `initialize`. | Any plugin message before that answer is a violation. |
| Protocol version | `initialize` carries `protocol: 1`; the answer is exactly `{protocol: 1}`. Any other version means the host does not start the plugin. | The answer's `protocol` is 1, else CONFORMANCE-FAILED. |
| Snapshot and trees at startup | After its `initialize` answer the plugin sends one `surface/setTree` per contributed surface (every `contributes.surfaces` entry except kinds `service` and `picker-provider`, plus every `contributes.quickSettings` entry, whose `surface` is the entry id) and at least one `snapshot`, in any order, within 2 s of the `initialize` request. The host renders a surface once it holds both. | All trees and a snapshot within the 2 s window; a `setTree` for a surface the manifest does not contribute is a failure. |
| Trees | A tree validates against `schemas/ui-tree.schema.json` and names only commands the plugin declares, or qualified commands its `call:` permissions cover. | Each tree is validated as it arrives. |
| Full snapshot | `data` is a JSON object and replaces the previous snapshot whole, nested objects and arrays included; a key the plugin omits is gone. Every absolute binding in the plugin's trees, and every `./` binding inside a `repeat` for each item of its array, resolves in the latest snapshot (a key whose value is `null` resolves); a binding with `source` reads another plugin's source and is not checked here. | Bindings resolve after startup and again before shutdown. |
| Requests and notifications | `snapshot`, `surface/setTree` and `source/snapshot` are notifications. `notify`, `toast`, `exec` and `compositor/command` are requests (they carry an id) and are always answered. `event` and `source/changed` are notifications; every other host → plugin method is a request. Either side may have several requests in flight; answers come in any order, and neither side blocks its input while it waits (a plugin may complete a `command/invoke` after its own `exec` request is answered). | A request method sent as a notification, an unknown plugin method, or an answer to an id the host never sent is a violation. |
| Answer deadline | The plugin answers every host request within 2 s. | Each request waits at most 2 s. |
| Error codes | `-32601` method not found (an unknown request; unknown notifications are ignored), `-32602` invalid params (including `command/invoke` of a command the manifest does not declare), `-32001` permission not declared. An error answer to a plugin's request never ends the plugin. | An undeclared command answers `-32602`; `conformance/unknown-method` answers `-32601`; the plugin is alive after every `-32001`. |
| Command results | `result: none` (the default) answers `{}`; `text` answers `{result: "<string>"}`; `json` answers `{result: <any JSON>}`. A command with a `verb` acts on a surface and is handled by views-shell; it is never sent to the plugin. | Each declared command without a verb is invoked once with valid args (`string` "conformance", `integer` 7, `number` 0.5, `boolean` true) from `source: "cli"`. |
| Other host requests | `surface/opened`, `surface/closed` and `config/changed` answer `{}`. | Each answered `{}`. |
| `config` | `initialize.config` and `config/changed.config` are the plugin's full configuration with every declared default filled in. | `config/changed` with every declared property changed. |
| `event` | Delivered only for names in `contributes.events`, as `{name, data}`. | One per declared event; the plugin stays alive. |
| Broker answers | `exec` answers `{exitCode, stdout, stderr}`; `notify`, `toast` and `compositor/command` answer `{}`. | The runner answers declared requests with these shapes and runs nothing. |
| Shutdown and grace period | `shutdown` is answered `{}`; the host then closes the plugin's stdin. The process exits with status 0 within 1 s of the `shutdown` request, or is killed. End of input on stdin alone also means shut down. | Answer `{}`, exit status 0, within 1 s (the end-of-input path alone is not exercised). |

## Settled by the C++ host (w3b, 2026-10-02)

`shell/plugins/` is the host side in C++ (`ProcessPlugin`, `PermissionsBroker`,
`PluginHost`). It keeps every row above and decides these points, which the
tables left open. No method was added.

| Question | Settled |
|---|---|
| Launch | `runtime.exec` (relative to the plugin directory) with `runtime.args`, working directory the plugin directory, stdin and stdout the protocol pipes, stderr logged line by line. Inside `systemd-run --user --scope --collect --unit=views-shell-plugin-<id>-<pid>-<n>` when `systemd-run` is on `PATH` and a probe scope (`-- true`) succeeds; otherwise plain `base::LaunchProcess`. The path taken is logged at every launch. Inside `runtime-test` there is no user manager, so the plain path is taken; the bench's FHS build environment reaches one, so an automatic launch there takes the scope path. |
| `initialize.capabilities` | The running adapter's capabilities that the plugin declares (`requires.compositor.required` ∪ `optional`), sorted. |
| `initialize.config`, `config/changed.config` | The declared defaults with the user's values merged over them. |
| Arguments | `command/invoke` is sent only for a declared command without a `verb`, with every required argument present, every argument of its declared type (`integer` also accepts an integral number), and no undeclared argument. Anything else is answered `-32602` by the host itself; the plugin never sees it. |
| Answer shape | An answer that does not match the command's `result` type (`text` without a string `result`, `json` without `result`, or a non-object) is reported to the caller as `-32603`; the plugin is not ended. |
| Host-side failures | `-32000`: no answer within 2 s, the plugin exited with the request in flight, the plugin is not running, no compositor adapter, or the compositor refused (`data: {error: <not-connected\|capability-missing\|invalid-argument\|rejected\|no-echo>}`). |
| `-32001` | Carries `data: {permission: "<the permission that was missing>"}`. |
| `exec` | The program is looked up on `PATH` (the permission names a bare program), stdin is `/dev/null`, stdout and stderr are captured up to 1 MiB each, and the deadline is 10 s. A program that cannot start answers `exitCode: 127` with the reason on `stderr`; one killed at the deadline answers `137`. `args` that is not a list of strings is `-32602` before any permission question. |
| `notify`, `toast` | `notify` needs a string `summary`; `urgency` is `low`, `normal` (the default) or `critical`. `toast` needs a string `text`. Otherwise `-32602`. |
| `compositor/command` | `{capability, args}` maps to one typed command: `workspaces.focus` (`workspace` id or `name`), `windows.focus` (`window`), `workspaces.rename` (`workspace`, `name`), `windows.move-to-workspace` (`window`, and `workspace` or `name`), `scratchpad.toggle`, `session.exit`, `config.reload`. Ids may be strings or integers. Other arguments are ignored. A capability with no typed command (`scroll.lua`, for example) is `-32602`. Answered `{}` once the compositor's echo arrived. |
| Before `initialize` | Anything the plugin sends before its `initialize` answer is dropped and logged. No answer within 2 s: the process is killed and counted as a crash. |
| Protocol mismatch | An `initialize` answer whose `protocol` is not the integer 1: the process is killed and never restarted (state failed). |
| Dropped notifications | `surface/setTree` for a surface the manifest does not contribute, a `snapshot` whose `data` is not an object, and `source/snapshot` for an undeclared source are dropped and logged; the plugin keeps running. |
| Crash and restart | A plugin that exits on its own is restarted after 0.5 s, doubling per consecutive crash up to 30 s; it is restarted at most 5 times in a row, and the sixth consecutive crash leaves it failed. A run that lasted 30 s resets the count. Requests in flight fail with `-32000`; requests made while it restarts wait for the next `initialize`. The host reports `{reason, exitCode, consecutive, restartIn or never, stderr tail}` for the plugin's surfaces. |
| Shutdown | `shutdown` with a 1 s deadline; stdin is closed on its answer (or at the deadline); the process is killed if it has not exited 1 s after the request. |
| `event`, `source/changed` | `event` only for names in `contributes.events`; `source/changed` only for the plugin's own sources or a declared `state:read:<plugin-id>/<source>`. Both only to a running plugin: one that is still starting gets the state from its next event. |
