# Notifications

views-shell is the session's notification daemon (docs/architecture.md §8). It
serves the freedesktop Desktop Notifications interface, version 1.2, on the
session bus and shows each notification with the stock `ui/message_center`
views. Every popup is a `zwlr_layer_surface_v1` (rule R1). The library lives in
[`../shell/notifications/`](../shell/notifications/) and is built as
`//views_shell/notifications:notifications`. The program starts it through
`NotificationService`; wiring it into `views_shell_main.cc` is the next item
(w3c).

## Pieces

| Class | File | Job |
|---|---|---|
| `NotificationServer` | `notification_server.{h,cc}` | exports `org.freedesktop.Notifications` at `/org/freedesktop/Notifications` with `dbus::ExportedObject`, converts each `Notify` into a `message_center::Notification`, adds, updates and removes it through `MessageCenter`, and emits `NotificationClosed` and `ActionInvoked` |
| `ShellMessagePopupCollection` | `shell_message_popup_collection.{h,cc}` | a `message_center::MessagePopupCollection` that stacks the popups top-down from the top right of the primary display's work area, and gives each popup Widget `Widget::InitParams::layer_shell` |
| `NotificationService` | `notification_service.{h,cc}` | initializes `MessageCenter`, owns the D-Bus thread, the session-bus connection, the server and the popups; `Start(use_session_bus, on_started)` and `Stop()` |

The bus is a `dbus::Bus` from `//dbus` (`SESSION`, `PRIVATE`), with its own
IO thread, never `//components/dbus` (rule R8: one owner per connection).
`//components/dbus` is not a dependency of the notifications target. It is
still in the program's closure, and so cannot be asserted out: Ozone's Wayland
platform reaches it through `//ui/base/clipboard:clipboard_util_linux`, which
calls the XDG FileTransfer portal (`dbus_xdg::FileTransferPortal`) when files
are copied or pasted. That connection belongs to the clipboard, not to this
library. Method calls and
signals reach the server on the bus's origin thread, which is the UI thread.
That is the thread `MessageCenter` and the popups live on.

## The mapping

| freedesktop 1.2 | views-shell |
|---|---|
| `GetCapabilities` | `body`, `actions`, `persistence` (no `body-markup`, `body-hyperlinks`, `body-images`, `icon-static`, `action-icons` or `sound`) |
| `GetServerInformation` | `views-shell`, `views-shell`, the version string the program passes (its commit), `1.2` |
| `Notify` → `u` | `message_center::Notification`, `NOTIFICATION_TYPE_SIMPLE`, id `views-shell-notification-<n>`; returns `n` (never 0) |
| `app_name` | `NotifierId(NotifierType::APPLICATION, app_name)` and the display source |
| `summary` | the title |
| `body` | the message, as plain text |
| `actions` (key, label pairs) | one `ButtonInfo(label)` per pair whose key is not `default`, in order; a pair with `default` makes a click on the body invoke `default`; an odd trailing key is dropped |
| `replaces_id` | when that id is still open, `MessageCenter::UpdateNotification` under the same id; otherwise a new id |
| `expire_timeout` = -1 | the server default: the popup closes after message_center's popup delay, and the notification stays in `MessageCenter` (persistence) |
| `expire_timeout` = 0 | `never_timeout`: the popup stays until it is closed |
| `expire_timeout` > 0 | removed after that many milliseconds; `NotificationClosed` reason 1 |
| hint `urgency` 0, 1 | `DEFAULT_PRIORITY` (message_center shows no popup below `DEFAULT_PRIORITY`, so low urgency is not demoted further) |
| hint `urgency` 2 | `SYSTEM_PRIORITY` and `never_timeout`; an explicit timeout does not remove it (critical notifications do not expire) |
| hint `resident` | an invoked action leaves the notification open |
| hints `transient`, `category`, `desktop-entry` | parsed and kept on the request; nothing uses them yet |
| any other hint | ignored |
| `CloseNotification(id)` | removed; `NotificationClosed(id, 3)`; an id that is not open gets an empty reply |
| a button or body click | `ActionInvoked(id, key)`, then, unless `resident`, the notification is dismissed: `NotificationClosed(id, 2)` |
| closed by the user | `NotificationClosed(id, 2)` |
| removed any other way | `NotificationClosed(id, 4)` |

A malformed `Notify` or `CloseNotification` gets
`org.freedesktop.DBus.Error.InvalidArgs`. Name ownership is requested with
`REQUIRE_PRIMARY` after all four methods are exported. If another daemon owns
the name on this bus, `Start`'s callback gets `false`, and the popups still
work for in-process callers.

## Popups as layer surfaces

`ShellMessagePopupCollection::ConfigureWidgetInitParamsForContainer` runs once
per popup, before its Widget is created. It computes the popup's slot, which is
the next slot below the popups already shown, at message_center's notification
width (360) and the popup's own height. It then sets `params->bounds` (the
surface size) and `params->layer_shell` from a `PopupSurfaceSpec`:

| field | value |
|---|---|
| layer | overlay |
| anchor | top and right |
| margins | top: slot y minus the work area's top; right: the work area's right minus the slot's right (`kMarginBetweenPopups` for the first popup); bottom and left 0 |
| exclusive zone | 0 (a popup never moves tiled windows; the compositor still keeps it clear of the bar's zone) |
| keyboard | none |
| namespace | `views-shell-notification` |

The work area comes from `display::Screen`'s primary display. On Wayland that
is the output's bounds. The compositor itself places an anchored surface with
exclusive zone 0 inside the area other surfaces leave free. Popups are
`TYPE_POPUP` Widgets without a parent. In the program the `ViewsDelegate` makes
each one a desktop widget, so `DesktopWindowTreeHostLinux` turns the
`layer_shell` field into `PlatformWindowType::kLayerShell`.

## Tests

`views_shell_unittests --gtest_filter='Notification*:ShellMessagePopup*'`:

| Suite | Needs | What |
|---|---|---|
| `NotificationServerMappingTest` | nothing | the table above, through the in-process core, with mock time for expiry |
| `NotificationServerBusTest` | a session bus | a second `dbus::Bus` as a client: `GetServerInformation`, `GetCapabilities`, `Notify` reaching `MessageCenter`, `InvalidArgs`, `CloseNotification` emitting `NotificationClosed(id, 3)`, a button emitting `ActionInvoked`. Skips with a message when `DBUS_SESSION_BUS_ADDRESS` is unset |
| `ShellMessagePopupSurfaceSpecTest` | nothing | the layer-surface request for a slot |
| `ShellMessagePopupCollectionTest`, `NotificationServiceTest` | a Wayland display | `views::ViewsTestBase` with real `MessagePopupView` widgets: a popup appears on `Notify`, popups stack, `CloseNotification` closes one, buttons map to action keys, the service starts and stops without a bus. Skips with a message when there is no display |

The Views tests need a display because `aura::Env` initializes Ozone, and
this build has Wayland as its only Ozone platform (`ozone_auto_platforms =
false`; there is no headless platform). `tools/bench/seq/w2c.sh` (stage
`display`) therefore runs them inside `runtime-test` under `dbus-run-session`,
against a private headless scroll. The test windows are aura test hosts, which
ignore `layer_shell`. These tests therefore check the request as data
(`surface_specs()`), not as protocol. The protocol is the same path the bar
already proves (`get_layer_surface` in the T4 and w1a runs). The umbrella's
`main` is `//base/test:run_all_unittests`. The Views tests do the setup a Views
test suite does (mojo, GL test support, `ui_test.pak`, a discardable memory
allocator, an `AXPlatformForTest`) once per process.

## Not done

- **Icons.** `app_icon` (a name or a `file://` path) and the `image-path`,
  `image-data` and `icon_data` hints are read past and not drawn.
  `GetCapabilities` does not claim `icon-static`.
- **Body markup.** The body is shown as plain text. A client that sends
  `<b>` despite the missing `body-markup` capability gets the tags shown
  literally. Hyperlinks and images in the body are not done.
- **Inline replies.** There is no `inline-reply` capability and no text field.
  Its keyboard on a layer surface is the credential modal's risk (w3c).
- **Restacking.** A layer surface is placed by the compositor from its anchor
  and margins. The margins are fixed when the popup is created. When a popup
  above closes, message_center moves the others up with `Widget::SetBounds`,
  which resizes the surface but does not move it. The merged layer-shell patch
  has `WaylandLayerShellWindow::SetMargin` but no Widget-level path to it.
  Closing a popup therefore leaves a gap until the popups below it close.
- **Sound, `action-icons`, `x`/`y` hints and the notification center view.**
  None of them is done. Persistence keeps the notifications in `MessageCenter`
  for a center view to list later.
- **`CloseNotification` on an unknown id.** It answers an empty reply, not the
  specification's "empty D-Bus error" (which has no error name to send).
- **Multiple outputs.** Popups go to the compositor's choice of output
  (`output_id` unset) and follow the primary display's work area.
