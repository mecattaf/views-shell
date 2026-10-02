// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The notifications surface as one object the program starts and stops
// (docs/architecture.md §8, docs/notifications.md): MessageCenter, the
// freedesktop server on its own session-bus connection, and the layer-shell
// popups.
//
// Threads. Construct, Start, Stop and destroy on the UI thread, after the
// bootstrap has made aura::Env, the ViewsDelegate and display::Screen (the
// popups are Widgets). The session bus connection lives on a dedicated
// "views-shell D-Bus" IO thread that this object starts and joins; method
// calls and signals reach the server on the UI thread (the bus's origin).
//
// Rule R8: this connection is the notifications daemon's own. It is a
// dbus::Bus (//dbus), never //components/dbus.

#ifndef VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVICE_H_
#define VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVICE_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

namespace base {
class Thread;
}

namespace dbus {
class Bus;
}

namespace views_shell::notifications {

class NotificationServer;
class ShellMessagePopupCollection;

class NotificationService {
 public:
  // |server_version| is GetServerInformation's version field: the commit the
  // program was built from.
  explicit NotificationService(std::string server_version);
  NotificationService(const NotificationService&) = delete;
  NotificationService& operator=(const NotificationService&) = delete;
  ~NotificationService();  // Stops if still started.

  // UI thread. Initializes MessageCenter (unless the process already has
  // one, which then stays the caller's), creates the popup collection and
  // the server. With |use_session_bus| it also starts the D-Bus thread,
  // connects to the session bus and exports org.freedesktop.Notifications;
  // |on_started| then gets whether the name was acquired. Without it, or when
  // DBUS_SESSION_BUS_ADDRESS is unset, the popups still work for in-process
  // callers and |on_started| gets false. |on_started| runs on the UI thread,
  // asynchronously.
  void Start(bool use_session_bus, base::OnceCallback<void(bool)> on_started);

  // UI thread. Unexports, shuts the bus down (blocking until the D-Bus
  // thread has closed the connection), joins the thread, closes every popup
  // and shuts MessageCenter down if Start initialized it.
  void Stop();

  bool started() const { return started_; }

  // Valid between Start and Stop.
  NotificationServer* server() { return server_.get(); }
  ShellMessagePopupCollection* popups() { return popups_.get(); }

 private:
  void OnExported(bool success);

  const std::string server_version_;
  bool started_ = false;
  bool owns_message_center_ = false;
  std::unique_ptr<base::Thread> dbus_thread_;
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<NotificationServer> server_;
  std::unique_ptr<ShellMessagePopupCollection> popups_;
  base::OnceCallback<void(bool)> on_started_;
  base::WeakPtrFactory<NotificationService> weak_factory_{this};
};

}  // namespace views_shell::notifications

#endif  // VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVICE_H_
