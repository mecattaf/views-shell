# The scroll and sway adapter (new code, not written yet)

scroll is the default compositor and this is the first adapter views-shell writes. sway
shares the IPC and rides along. Nothing here has been written beyond
[`scroll_ipc_client.h`](scroll_ipc_client.h), a sketch.

## Shape

Modelled on the lifted niri client (`../niri/niri_ipc_client.*`): an
`FdWatcher` on a non-blocking AF_UNIX socket, a frame decoder that tolerates
splits across reads, reconnect with backoff, and a full resync on reconnect.

| Concern | Rule |
|---|---|
| Socket | `$SCROLLSOCK`, then `scroll --get-socketpath`, then `$SWAYSOCK`, then `$I3SOCK`; if all are missing, scan `$XDG_RUNTIME_DIR` for `scroll-ipc.*.sock` and `sway-ipc.*.sock` |
| Framing | `i3-ipc` magic (6 bytes), uint32 length, uint32 type, native byte order, JSON payload |
| Connections | two: one subscribed for events, one for requests |
| Order | subscribe first (`workspace`, `window`, `output`, `mode`, `binding`, `shutdown`, `tick`), then `GET_WORKSPACES`, `GET_TREE`, `GET_OUTPUTS` |
| Coalescing | at most one `GET_TREE` in flight; events that arrive meanwhile mark the tree dirty |
| Commands | `RUN_COMMAND` with criteria built by the adapter from typed `WmCommand`s, never from caller strings |
| Barrier | after a command, `SEND_TICK` with a unique payload; the matching `tick` event means the command's events have all been delivered |
| Bindings | `binding` events whose command starts with `nop views-shell ` are dispatched to the command registry |
| Reload | a `workspace` event with `change: "reload"` triggers a full resync and is relayed to Chrome |
| scroll-only | `GET_SCROLLER`, `GET_TRAILS`, `GET_SPACES`, `GET_BINDINGS` and the `scroller` and `trails` events back the `scroll.*` capabilities, and are used only when a plugin declares them. `LUA_EVAL` is used only for `scroll.lua.eval`, never from Chrome or the localhost service. |

Message type numbers are in `scroll/include/ipc.h` (scroll is MIT; read, not copied).

## Tests

- Unit: a fake server replaying recorded transcripts, including a frame split
  across reads and a reconnect mid-snapshot.
- Run gate: under `runtime-test`, a nested headless scroll (once packaged), with
  every compositor socket variable unset first.
