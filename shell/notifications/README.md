# shell/notifications

The session's notification daemon, as a library: a freedesktop Desktop
Notifications 1.2 server on its own session-bus connection (`//dbus`), feeding
`ui/message_center`. Its popups are layer-shell surfaces. The mapping, the
tests and what is not done are in
[`../../docs/notifications.md`](../../docs/notifications.md).

| File | What |
|---|---|
| `notification_server.{h,cc}` | `NotificationServer`: the exported interface, the freedesktop → `message_center::Notification` conversion, `NotificationClosed` / `ActionInvoked`, and an in-process `Observer` for the same two echoes |
| `shell_message_popup_collection.{h,cc}` | `ShellMessagePopupCollection`: popups top-down from the top right, each Widget given `Widget::InitParams::layer_shell` from a `PopupSurfaceSpec` (overlay, top and right, stacking margins, no keyboard, `views-shell-notification`) |
| `notification_service.{h,cc}` | `NotificationService`: `MessageCenter`, the D-Bus thread and bus, the server and the popups, started and stopped as one |
| `*_unittest.cc` | linked into `views_shell_unittests` through `:unittests` |

## Using it from the program

```cpp
#include "views_shell/notifications/notification_service.h"

// UI thread, after the bootstrap (aura::Env, ViewsDelegate, display::Screen).
views_shell::notifications::NotificationService notifications(kCommit);
notifications.Start(/*use_session_bus=*/true,
                    base::BindOnce([](bool owned) {
                      LOG_IF(WARNING, !owned) << "not the notification daemon";
                    }));
...
notifications.Stop();  // UI thread, before the bootstrap tears Views down.
```

`Start` initializes `MessageCenter` unless one exists. `Stop` joins the D-Bus
thread and shuts `MessageCenter` down if `Start` initialized it.
`server()->AddObserver()` gives in-process callers the close and action
echoes. `server()->Notify()` lets the shell post its own notifications without
going over the bus.

## Testing it

On the bench, under the bench lock: `tools/bench/seq/w2c.sh` (stages `build`,
`display`, `bus`, `nobus`). The Views tests need a Wayland display, and the D-Bus
tests need a session bus. Without either, they skip and print why.
