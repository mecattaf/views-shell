# Compositor adapters

views-shell observes the compositor and asks it to act. It never owns windows. Each
adapter turns one compositor's IPC into the compositor-neutral `WmModel`, and
turns views-shell commands into that compositor's requests. A config writer sits beside
each adapter and owns that compositor's slot files.

## Lineage: Noctalia

"One adapter per compositor" comes from the Noctalia era of Tom's desktop. noctalia
(`noctalia-dev/noctalia`, MIT; v5 is a from-scratch C++ shell on Wayland and GLES,
v4 was Quickshell/QML) keeps one directory per compositor under `src/compositors/`
(`dwl`, `ext_workspace`, `hyprland`, `kde`, `labwc`, `mango`, `niri`, `sway`,
`umbriel`), behind three small interfaces (`WorkspaceBackend`,
`WorkspaceMetadataBackend`, keyboard and output backends), detected by environment
variable in a fixed order (`compositor_detect.cpp:30-45`, read at v5.2.0
`30415127`). Its sway backend switches to `scrollmsg` when that binary exists, and
its generic `ext_workspace` backend is the shipping fallback for everything else.

views-shell keeps the idea and changes two things. Capabilities are **declared** in a
table and narrowed by a probe, where noctalia negotiates by absence (virtual
defaults that return empty); a plugin must know before it registers. And every
command waits for its **observed echo** (a barrier per compositor), where noctalia
re-reads on the next change callback. Nothing is copied from noctalia; it is read
as design reference (MIT).

## The interface

The interface is
[`../shell/wm/compositor_adapter.h`](../shell/wm/compositor_adapter.h); the scroll
and sway adapter implements it (see "Real today" below). Every adapter:

1. **Connects** on an IO thread. It finds its socket from the environment, and when
   the variable is missing (views-shell started outside the session) it scans
   `$XDG_RUNTIME_DIR`.
2. **Subscribes before it queries**, so no event is lost between the snapshot and
   the subscription.
3. **Publishes a full snapshot** (outputs, workspaces, windows, focus, fullscreen,
   urgency) to the `WmModel` on the UI thread. It never publishes a patch.
4. **Reconnects with backoff**, and resynchronises the full snapshot on reconnect.
5. **Sends commands** and reports when they have landed. A command is never
   assumed to have worked. The redraw waits for the observed echo (rule R6).
6. **Declares capabilities**: a static table, refined by a probe at connect (IPC
   version, Wayland globals present).

Windows are joined across IPC and Wayland by a reference that pairs the adapter's
window id with the `ext-foreign-toplevel-list` identifier.

## scroll and sway

| | |
|---|---|
| Socket | `$SCROLLSOCK`, then `scroll --get-socketpath`, then `$SWAYSOCK`, then `$I3SOCK` (as scroll's `common/ipc-client.c` resolves it) |
| Framing | i3-ipc binary: magic `i3-ipc`, then 32-bit length and 32-bit type, native byte order, then a JSON payload |
| Two sockets | one connection for requests, one for the subscription, so a slow reply never blocks events |
| Subscribe | `SUBSCRIBE ["workspace","window","output","mode","binding","shutdown","tick"]`, and on scroll also `"scroller"` and `"trails"` when a plugin declares those capabilities |
| Snapshot | `GET_WORKSPACES`, `GET_TREE` (coalesced: one in flight at a time), `GET_OUTPUTS` |
| Commands | `RUN_COMMAND` (`IPC_COMMAND`, type 0), for example `workspace number 3`, `[con_id=42] focus`, `rename workspace 3 to "web"` |
| Landed barrier | `SEND_TICK` with a unique payload after a command. The matching `tick` event means every event the command caused has been delivered before it. |
| Key dispatch | `bindsym $mod+d nop views-shell launcher`: the `nop` command emits a `binding` event that views-shell reads. Nothing forks per keypress, and the binding is harmless when views-shell is not running. |
| Gestures | Gestures emit no binding event on stock scroll (`ipc-json.c:1751-1795` falls to its default branch), so they use `exec views-shell call …` until hook H4 lands |
| Config | `GET_CONFIG` returns the last-loaded config text. `reload` validates first and keeps the old config on failure. A `workspace` event with change `reload` follows. |
| scroll-only types | `GET_SCROLLER` (120), `GET_TRAILS` (121), `GET_SPACES` (122), `GET_BINDINGS` (123), `LUA_EVAL` (124); events `scroller` (`0x8000001e`), `trails` (`0x8000001f`), `lua` (`0x8000001d`), `log` (`0x8000001c`). Source: `scroll/include/ipc.h`. |

Sway numbers workspaces globally. Per-monitor workspace numbering is done by views-shell's
naming, not by the compositor.

The adapter is new code in `shell/wm/adapters/scroll/` (see "Real today"). Its
shape followed the niri client once lifted from agency-mvp and deleted in chapter 2
(`git show 93d691a:shell/wm/adapters/niri/niri_ipc_client.cc`): a watched socket, a
frame decoder, reconnect with backoff, and a full resync on reconnect.

## Real today (chapter 2, w2a)

The scroll and sway adapter exists as code in the wm seam (`shell/wm/BUILD.gn`,
target `//views_shell/wm:wm`, linked into `views_shell`). It depends on `//base`
only (plus `gfx::Rect` for the hook-event shapes), never on Views or Wayland.

| Piece | File | What it does |
|---|---|---|
| Transport | `adapters/scroll/scroll_ipc_client.{h,cc}` | Two AF_UNIX connections on its own IO thread (`base::IOWatcher`), 14-byte i3-ipc framing that tolerates split and coalesced frames, `SUBSCRIBE` on the events connection, any request type on the requests connection with replies in order, `base::JSONReader` on the IO thread, delivery on the caller's sequence through a `WeakPtr`. One client is one connection attempt. |
| Socket | `ScrollIpcClient::ResolveSocketPath()` | `$SCROLLSOCK`, `scroll --get-socketpath`, `$SWAYSOCK`, `$I3SOCK`, then the newest `scroll-ipc.*.sock`, then `sway-ipc.*.sock` in `$XDG_RUNTIME_DIR`. An empty variable counts as unset. |
| Adapter | `adapters/scroll/scroll_adapter.{h,cc}` | Subscribes to `workspace`, `window`, `output`, `binding`, `shutdown`, `tick`; then `GET_VERSION`; then one refresh (`GET_OUTPUTS`, `GET_WORKSPACES`, `GET_TREE`, pipelined) folded into a `WmSnapshot`. One refresh in flight; events meanwhile set a dirty flag. |
| Commands | `CommandText()` in `scroll_adapter.cc` | The only place command strings are spelled: `workspace --no-auto-back-and-forth "<name>"`, `[con_id=N] focus`, `rename workspace "<a>" to "<b>"`, `[con_id=N] move container to workspace "<w>"`, `scratchpad show`, `exit`, `reload`. Names are double-quoted with `\` and `"` escaped; window ids must be digits and present in the current snapshot. `--no-auto-back-and-forth` keeps `workspace_auto_back_and_forth` in a user's config from bouncing a focus request. |
| Barrier | `ScrollAdapter::Send()` | After a successful reply, `SEND_TICK` with `views-shell-<pid>-<serial>`. The matching `tick` event marks every event the command caused as received; the command is done when a snapshot requested after the last of them has been delivered. A refused command fails with `kRejected` at once; no tick within `command_timeout` (5 s) is `kNoEcho`; `exit` is echoed by the `shutdown` event. |
| Bindings | `ScrollAdapter::OnEvent()` | `binding` events whose command starts with `nop views-shell ` reach `Delegate::OnBinding` with the rest of the command. |
| Reload | | A `workspace` event with change `reload` is relayed as `OnConfigReloaded(true)`; a refused `reload` as `OnConfigReloaded(false, error)`. |
| Reconnect | | Either connection dropping fails pending commands with `kNotConnected`, tells the delegate (when it had connected), and reconnects after a backoff (250 ms doubling to 10 s); the first snapshot after it is a full resync. |
| Model | `wm_model.{h,cc}` | Diffs successive snapshots into `OnChildAdded`, `OnChildRemoved`, `OnChildMoved` (workspaces under the root, windows under their workspace, scratchpad windows under `@scratchpad`), then `OnItemChanged`, `OnOutputsChanged`, `OnMruChanged`, `OnFocusChanged`. Requests go to the adapter; the model changes only on the echo. |
| Gate | `wm_probe.cc` | Console program, `//base` and `:wm` only. `--dump` prints the first snapshot as JSON; `--switch <name>` focuses through `WmModel` and prints `ECHO workspace <name>` on the echo; `--watch <s>` prints every model change. |

Snapshot keys are scroll's node ids (unique across all nodes of a session), as
`docs/architecture.md` §6 asks. Outputs are keyed by connector name. Windows are
the tree's leaf views; their `column` is the index of the workspace's tiling child
that holds them, which on scroll is the column. The MRU starts as the tree's focus
stacks flattened depth first and is then kept by `window` focus events.
`toplevel_id` stays empty: i3-ipc does not report the ext-foreign-toplevel-list
identifier.

**How capabilities are narrowed.** `ProbeCapabilities()` starts from the "yes"
rows of the Capabilities table below for the variant: `scroll` when `GET_VERSION` carries
`"variant": "scroll"`, `sway` otherwise. The "with ask Hn" rows (`overview.events`
H2, `windows.geometry-events` H3, `bindings.gesture-events` H4) are kept only when
`GET_VERSION` carries a `features` array that names them, by capability name or by
hook id. The array is views-shell's proposed shape for H10; stock scroll sends none,
so on the bench the hook rows are absent. A name in `features` that is not a row of
the table is ignored: the probe never invents a capability. No command needs a
scroll-only capability, so the sway set refuses nothing the adapter can spell.

**Tests.** `views_shell_unittests` runs the transport against a scripted server on
a real socket (split and coalesced frames, pipelined replies in order, a close
with requests pending, a missing socket, the resolution order) and the adapter
against a fake compositor that replays
`shell/wm/adapters/scroll/testdata/scroll-transcript.jsonl` (recorded from headless
stock scroll by `testdata/record_transcript.py`) by epoch, checking each command
string byte for byte, a reconnect mid-snapshot, a missing tick, bindings, and the
static capability and spelling tables. `WmModel` is tested on hand-written
snapshots, including a mirror observer that must end up holding each snapshot's
order. The bench record is
[`bench/views-shell-154-adapter.md`](bench/views-shell-154-adapter.md).

Not yet: the scroll-only requests (`GET_SCROLLER`, `GET_TRAILS`, `GET_SPACES`,
`GET_BINDINGS`) and their events, `mode` events, the config writer, the hook
events (H1 to H3) and the ext-foreign-toplevel join. No surface uses the model yet;
the first is the rail.

## niri

| | |
|---|---|
| Socket | `$NIRI_SOCKET` |
| Framing | one JSON request per line, one JSON reply per line |
| Subscribe | request `"EventStream"` on a dedicated connection. The stream first sends the full state, then events. |
| Events | `WorkspacesChanged`, `WorkspaceActivated`, `WorkspaceActiveWindowChanged`, `WorkspaceUrgencyChanged`, `WindowsChanged`, `WindowOpenedOrChanged`, `WindowClosed`, `WindowFocusChanged`, `WindowUrgencyChanged`, `WindowLayoutsChanged`, `KeyboardLayoutsChanged`, `OverviewOpenedOrClosed`, `ConfigLoaded` (also sent once at connect) |
| Snapshot | `Workspaces`, `Windows`, `Outputs`, `FocusedWindow` |
| Commands | `{"Action": {"FocusWorkspace": {"reference": {"Id": 3}}}}`, `MoveWindowToWorkspace`, `SetWorkspaceName`, `FocusWindow` |
| Landed barrier | the `Handled` reply, then the matching event on the stream |
| Key dispatch | niri has no binding events. Bindings use `spawn "views-shell" "call" …`. |
| Config | `config.kdl` with an include of the generated `views-shell.kdl`. niri reloads on change, or on `LoadConfigFile`. `ConfigLoaded {failed}` reports the result. |
| Known gaps | no scratchpad; no list-bindings request, so the cheat sheet parses the config |

The 2026-06-27 switcher used `niri msg -j event-stream` and
`niri msg action focus-workspace <id>` as subprocesses. views-shell speaks the socket in
process instead. Source for the shapes: `niri-ipc/src/lib.rs` (read only; niri is
GPL-3.0 and is never copied).

## Generic: ext-workspace and ext-foreign-toplevel (PROPOSED third place)

For any compositor with layer-shell plus `ext-workspace-v1` and
`ext-foreign-toplevel-list-v1` (labwc, river, Wayfire, COSMIC's compositor and
others), with no IPC at all. noctalia ships this path today as its fallback, which
is the evidence that it is worth building before Hyprland (PROPOSED order, open
decision P20).

| | |
|---|---|
| Observe | `ext_workspace_manager_v1` groups and handles (one group per output); `ext_foreign_toplevel_list_v1` handles with their `identifier` |
| Commands | `activate` on a workspace handle; window focus only where `zwlr_foreign_toplevel_management_v1` is also offered |
| Landed barrier | the next `done` event of the manager after the request |
| Key dispatch | none: bindings use the compositor's own `exec views-shell call …` |
| Config | none: the writer for this adapter writes nothing |
| Known gaps | no rename, no move-to-workspace, no bindings list, no scratchpad; on stock scroll, workspace handles carry a NULL id and `REMOVE` is a no-op, which hook H5 fixes |

## Hyprland

| | |
|---|---|
| Sockets | `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock` (requests) and `.socket2.sock` (events) |
| Requests | one request per connection, `j/` prefix for JSON: `j/workspaces`, `j/clients`, `j/monitors`, `j/activewindow`, `j/binds` |
| Events | one line per event, `EVENT>>DATA`: `workspacev2`, `createworkspacev2`, `destroyworkspacev2`, `renameworkspace`, `openwindow`, `closewindow`, `movewindowv2`, `activewindowv2`, `focusedmonv2`, `fullscreen`, `urgent`, `configreloaded` |
| Commands | `dispatch workspace 3`, `dispatch focuswindow address:0x…`, `dispatch renameworkspace 3 web` |
| Landed barrier | the `ok` reply, then the matching event on socket2 |
| Key dispatch | `bind = SUPER, D, exec, views-shell call views-shell.launcher toggle` |
| Config | sourced `views-shell.conf`. Recent Hyprland moved to a Lua config. The writer targets whichever the probed version uses. |

## Capabilities

Capabilities are declared, never assumed. A plugin entry that requires a capability
the compositor lacks is not registered, and the control pages name the compositor
that provides it.

| Capability | scroll | sway | niri | Hyprland |
|---|---|---|---|---|
| `outputs.list` | yes | yes | yes | yes |
| `workspaces.list` | yes | yes | yes | yes |
| `workspaces.focus` | yes | yes | yes | yes |
| `workspaces.rename` | yes | yes | yes | yes |
| `windows.list` | yes | yes | yes | yes |
| `windows.focus` | yes | yes | yes | yes |
| `windows.move-to-workspace` | yes | yes | yes | yes |
| `windows.fullscreen-state` | yes | yes | yes | yes |
| `windows.urgency` | yes | yes | yes | yes |
| `bindings.events` (the `nop` dispatch) | yes | yes | no | no |
| `bindings.list` | yes (`GET_BINDINGS`) | config text only | no | yes (`binds`) |
| `config.reload` | yes | yes | yes | yes |
| `config.include-slot` | yes | yes | yes (niri versions with `include`) | yes |
| `scratchpad.toggle` | yes | yes | no | yes (special workspaces) |
| `overview.toggle` | yes (`scale_workspaces`) | no | yes | no |
| `session.exit` | yes | yes | yes | yes |
| `scroll.scroller` | yes | no | no | no |
| `scroll.trails` | yes | no | no | no |
| `scroll.spaces` | yes | no | no | no |
| `scroll.jump` | yes | no | no | no |
| `scroll.overview` | yes | no | no | no |
| `scroll.lua` | yes (reviewed permission) | no | no | no |
| `overview.events` | with ask H2 | no | yes (`OverviewOpenedOrClosed`, no geometry) | no |
| `windows.geometry-events` | with ask H3 | no | yes (`WindowLayoutsChanged`) | partial (`movewindowv2`, resize) |
| `bindings.gesture-events` | with ask H4 | with H4 via sway | no | no |
| `windows.placement-hook` | no | no | no | no |
| `windows.border-colour-rule` | no | no | yes | yes (`bordercolor` rule) |

Rows marked "with ask Hn" are "no" on the stock scroll the dotfiles pin, and become
"yes" only on a scroll build that has merged that upstream wish; the adapter learns
it from the `GET_VERSION` `features` array (ask H10).

The table is the adapter's static declaration. The probe at connect can only
remove a capability, never add one. Rows marked "yes" for niri and Hyprland are
expected from their IPC references and are unproven until those adapters run.

### Alt+Tab

On scroll and sway, the window cycle keeps its keys in a scroll mode bound to the
release of Alt. On niri and Hyprland, the strip takes the keyboard instead.

## scroll upstream wishes

scroll is stock and unpatched, and nothing above needs anything from dawsers. What
would make the scroll adapter better is a short list of listener-guarded IPC
additions at stable call sites, in this order: gesture binding events (H4), an
`app_id` change event (H11), jump begin and end with labels (H1), a global overview
event with workspace rects (H2), a `GET_VERSION` feature list (H10), ext-workspace
ids and `REMOVE` (H5), and a coalesced geometry event (H3). The earlier ask for "a
reorder command" is dropped, because scroll already has `workspace swap`, and blur is
not asked for. Until a `jump` end event exists, the core uses a minimal scroll Lua
callback for that one signal (rule R13).

Those asks, with their sizes, call sites and degraded paths, are written up in
[`scroll-fork-hooks.md`](scroll-fork-hooks.md). They are wishes for upstream only:
this repository carries no scroll source and no scroll patch, and a declined ask
changes nothing here. Sending them to dawsers is outward-facing and still open
(decision P16).

## Testing

Adapters are tested in two ways:
- **Unit tests** against recorded IPC transcripts. The rail's 2026-07-17 test
  snapshot becomes a fixture.
- **Run gates** under a nested headless compositor inside `runtime-test`. The
  harness unsets `SCROLLSOCK`, `SWAYSOCK`, `I3SOCK`, `NIRI_SOCKET` and
  `HYPRLAND_INSTANCE_SIGNATURE` first, so a test can never reach the live
  session's compositor. The harness is `tools/bench/worker/headless.sh` (a headless
  stock scroll under runtime-test); the gate it runs is `wm_probe` (below).

The first acceptance target is a rail click that switches a workspace and is
observed through the echo. No Views switcher has done that yet.
