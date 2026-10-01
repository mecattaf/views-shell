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
