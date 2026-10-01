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

A sketch lives in
[`../shell/wm/compositor_adapter.h`](../shell/wm/compositor_adapter.h). Every adapter:

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

The adapter is new code. Its template is the niri client lifted from agency-mvp
(`shell/wm/adapters/niri/`): an `FdWatcher` on the socket, a line or frame decoder,
reconnect with backoff, and a full-vector resync on reconnect.

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
| `overview.events` | with hook H2 | no | yes (`OverviewOpenedOrClosed`, no geometry) | no |
| `windows.geometry-events` | with hook H3 | no | yes (`WindowLayoutsChanged`) | partial (`movewindowv2`, resize) |
| `bindings.gesture-events` | with hook H4 | with H4 via sway | no | no |
| `windows.placement-hook` | with hook H7 (fork only) | no | no | no |
| `windows.border-colour-rule` | with hook H9 | no | yes | yes (`bordercolor` rule) |

Rows marked "with hook Hn" are true only on a scroll build carrying that patch,
and the adapter learns it from the `GET_VERSION` `features` array (hook H10). On
stock scroll they are "no".

The table is the adapter's static declaration. The probe at connect can only
remove a capability, never add one. Rows marked "yes" for niri and Hyprland are
expected from their IPC references and are unproven until those adapters run.

### Alt+Tab

On scroll and sway, the window cycle keeps its keys in a scroll mode bound to the
release of Alt. On niri and Hyprland, the strip takes the keyboard instead.

## scroll upstream, and a fork with hooks (experimental)

Nothing above needs anything from dawsers. What would make the scroll adapter
better is a short list of listener-guarded IPC additions at stable call sites, in
this order: gesture binding events (H4), an `app_id` change event (H11), jump
begin and end with labels (H1), a global overview event with workspace rects (H2),
a `GET_VERSION` feature list (H10), ext-workspace ids and `REMOVE` (H5), and a
coalesced geometry event (H3). The earlier ask for "a reorder command" is dropped,
because scroll already has `workspace swap`, and blur is not asked for. Until a
`jump` end event exists, the core uses a minimal scroll Lua callback for that one
signal (rule R13).

Carrying those as patches, and adding hooks dawsers would decline (a synchronous
placement hook, per-window border colours, blur), is the experimental scroll fork
with hooks described in [`scroll-fork-hooks.md`](scroll-fork-hooks.md). Whether to
build the patch queue and whether to send anything upstream are open decisions
(P15, P16).

## Testing

Adapters are tested in two ways:
- **Unit tests** against recorded IPC transcripts. The rail's 2026-07-17 test
  snapshot becomes a fixture.
- **Run gates** under a nested headless compositor inside `runtime-test`. The
  harness unsets `SCROLLSOCK`, `SWAYSOCK`, `I3SOCK`, `NIRI_SOCKET` and
  `HYPRLAND_INSTANCE_SIGNATURE` first, so a test can never reach the live
  session's compositor. See [`../shell/tools/headless-eval.sh`](../shell/tools/headless-eval.sh).

The first acceptance target is a rail click that switches a workspace and is
observed through the echo. No Views switcher has done that yet.
