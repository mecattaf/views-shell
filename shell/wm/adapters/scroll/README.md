# The scroll and sway adapter

scroll is the default compositor and this is the first adapter views-shell
writes. sway shares the IPC and rides along: the adapter tells them apart by the
`variant` field of `GET_VERSION` (scroll sends `"scroll"`, sway sends none).

| File | Role |
|---|---|
| [`scroll_ipc_client.h`](scroll_ipc_client.h), `.cc` | The i3-ipc transport: two AF_UNIX connections on an IO thread, framing, replies in order, socket resolution |
| [`scroll_adapter.h`](scroll_adapter.h), `.cc` | `CompositorAdapter` for scroll and sway: snapshots, typed commands, the `SEND_TICK` barrier, bindings, reconnect, the capability probe |
| `scroll_ipc_client_unittest.cc` | The transport against a scripted server on a real socket |
| `scroll_adapter_unittest.cc` | The adapter against a fake compositor replaying `testdata/scroll-transcript.jsonl` |
| [`testdata/`](testdata/README.md) | The recorded transcript and its recorder |

The run gate is `../../wm_probe.cc` (`//views_shell/wm:wm_probe`). What the
adapter implements, command by command, is in
[`docs/compositor-adapters.md`](../../../../docs/compositor-adapters.md), section
"Real today".

## Shape

| Concern | Rule |
|---|---|
| Socket | `$SCROLLSOCK`, then `scroll --get-socketpath`, then `$SWAYSOCK`, then `$I3SOCK`; if all are missing, the newest `scroll-ipc.*.sock`, then `sway-ipc.*.sock`, in `$XDG_RUNTIME_DIR` |
| Framing | `i3-ipc` magic (6 bytes), uint32 length, uint32 type, native byte order, JSON payload (raw text for `RUN_COMMAND` and `SEND_TICK`) |
| Connections | two: one subscribed for events, one for requests; one client per connection attempt |
| Order | subscribe first (`workspace`, `window`, `output`, `binding`, `shutdown`, `tick`), then `GET_VERSION`, then `GET_OUTPUTS`, `GET_WORKSPACES`, `GET_TREE` |
| Coalescing | at most one refresh (the three queries) in flight; events that arrive meanwhile mark the state dirty and send one more refresh when it lands |
| Commands | `RUN_COMMAND` with strings built by `CommandText()` from typed `WmCommand`s, never from caller strings |
| Barrier | after a command, `SEND_TICK` with a unique payload; the matching `tick` event means the command's events have all been received, and the command is done once a snapshot requested after them has been delivered |
| Bindings | `binding` events whose command starts with `nop views-shell ` go to `Delegate::OnBinding` |
| Reload | a `workspace` event with `change: "reload"` is relayed as `OnConfigReloaded(true)` and refreshes the snapshot |
| Reconnect | backoff 250 ms doubling to 10 s, full resync, pending commands fail with `kNotConnected` |
| scroll-only | `GET_SCROLLER`, `GET_TRAILS`, `GET_SPACES`, `GET_BINDINGS` and the `scroller` and `trails` events back the `scroll.*` capabilities and are not used yet. `LUA_EVAL` is absent from the client and will be used only for `scroll.lua.eval`, never from Chrome or the localhost service. |

Message type numbers are in `scroll/include/ipc.h` (scroll is MIT; read, not
copied); the enum in `scroll_ipc_client.h` carries the ones the adapter uses.

## Provenance

The transport started as the cancelled chapter-1 T5 branch
(`t/views-shell-t5-scroll-adapter`, never pushed): its two-connection client,
its frame decoder, the transcript format and the fake-server idea were kept.
Its fixed `GET_VERSION`/`GET_WORKSPACES`/`RUN_COMMAND` methods became one
`Request()`; delivery moved from `base::Unretained` observers to a `WeakPtr`
so nothing runs after the client is gone; the `views_shell_main.cc` demo
moved to `wm_probe --switch`.
