// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The freedesktop Desktop Notifications server (specification version 1.2,
// org.freedesktop.Notifications at /org/freedesktop/Notifications), feeding
// ui/message_center (docs/architecture.md §8, docs/notifications.md).
//
// NotificationServer owns no bus and no thread. It is given a dbus::Bus that
// is already connected to the session bus (NotificationService makes one), and
// a message_center::MessageCenter. Everything here runs on the bus's origin
// thread, which is the UI thread in the program: exported methods are
// delivered there, MessageCenter is touched only there.
//
// The core (Notify, CloseNotification) is also callable without D-Bus, which
// is how the popup tests drive it; the D-Bus layer only parses and replies.

#ifndef VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVER_H_
#define VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "dbus/exported_object.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_observer.h"

namespace dbus {
class Bus;
class MethodCall;
}  // namespace dbus

namespace message_center {
class Notification;
}

namespace views_shell::notifications {

// The well-known names of the specification.
inline constexpr char kNotificationsServiceName[] =
    "org.freedesktop.Notifications";
inline constexpr char kNotificationsObjectPath[] =
    "/org/freedesktop/Notifications";
inline constexpr char kNotificationsInterface[] =
    "org.freedesktop.Notifications";
inline constexpr char kNotificationsSpecVersion[] = "1.2";
inline constexpr char kServerName[] = "views-shell";
inline constexpr char kServerVendor[] = "views-shell";

// The MessageCenter id of freedesktop notification <n> is this prefix plus n.
inline constexpr char kMessageCenterIdPrefix[] = "views-shell-notification-";

// The reasons of the NotificationClosed signal (specification 1.2).
enum class CloseReason : uint32_t {
  kExpired = 1,
  kDismissedByUser = 2,
  kClosedByCall = 3,
  kUndefined = 4,
};

// The urgency hint (a byte, 0..2).
enum class Urgency : uint8_t {
  kLow = 0,
  kNormal = 1,
  kCritical = 2,
};

// One Notify call, parsed. The hints the server reads are fields; every
// other hint is accepted and ignored (docs/notifications.md lists them).
struct NotifyParams {
  NotifyParams();
  NotifyParams(const NotifyParams&);
  NotifyParams& operator=(const NotifyParams&);
  ~NotifyParams();

  std::string app_name;
  uint32_t replaces_id = 0;
  std::string app_icon;  // Read but not drawn (docs/notifications.md).
  std::string summary;
  std::string body;
  // Pairs: action key, then its label. An odd trailing key is dropped.
  std::vector<std::string> actions;
  // -1: the server's default; 0: never expires; > 0: milliseconds.
  int32_t expire_timeout = -1;

  // Hints.
  Urgency urgency = Urgency::kNormal;
  bool resident = false;
  bool transient = false;
  std::string category;
  std::string desktop_entry;
};

// Builds the message_center::Notification for freedesktop id |id|: type
// NOTIFICATION_TYPE_SIMPLE, NotifierId(APPLICATION, app_name), title from the
// summary, message from the body (plain text), display source the app name,
// one ButtonInfo per non-"default" action pair, priority from the urgency.
// |delegate| receives the clicks. Pure; exposed for the tests.
std::unique_ptr<message_center::Notification> BuildMessageCenterNotification(
    uint32_t id,
    const NotifyParams& params,
    scoped_refptr<message_center::NotificationDelegate> delegate);

// The message_center priority for an urgency: low and normal are
// DEFAULT_PRIORITY (a popup is shown; message_center shows none below
// DEFAULT), critical is SYSTEM_PRIORITY and never times out.
int PriorityForUrgency(Urgency urgency);

// The action keys of |actions| that become buttons, in button order (every
// key but "default").
std::vector<std::string> ButtonActionKeys(
    const std::vector<std::string>& actions);

class NotificationServer : public message_center::MessageCenterObserver {
 public:
  // Echoes of what happened to a notification, the same two events the
  // server sends as D-Bus signals. Called on the origin thread.
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnNotificationClosed(uint32_t id, CloseReason reason) {}
    virtual void OnActionInvoked(uint32_t id, const std::string& action_key) {}
  };

  // |message_center| must outlive the server. |server_version| is the third
  // field of GetServerInformation (the build's commit).
  NotificationServer(message_center::MessageCenter* message_center,
                     std::string server_version);
  NotificationServer(const NotificationServer&) = delete;
  NotificationServer& operator=(const NotificationServer&) = delete;
  ~NotificationServer() override;

  // Exports the four methods on |bus| at /org/freedesktop/Notifications, then
  // requests org.freedesktop.Notifications (REQUIRE_PRIMARY: another running
  // notification daemon makes this fail). |on_done| gets true when every
  // method is exported and the name is owned. Call once, on the bus's origin
  // thread. Without this call the server works without D-Bus (no signals).
  void Export(scoped_refptr<dbus::Bus> bus,
              base::OnceCallback<void(bool)> on_done);

  // Stops answering D-Bus: unregisters the object and releases the name. The
  // bus itself belongs to the caller. Notifications stay in MessageCenter.
  void Unexport();

  // The core of Notify: adds or (with a live replaces_id) updates the
  // notification and returns its id (never 0).
  uint32_t Notify(const NotifyParams& params);

  // The core of CloseNotification: removes |id| and emits NotificationClosed
  // with reason kClosedByCall. Returns false when |id| is not open.
  bool CloseNotification(uint32_t id);

  // GetCapabilities: body, actions, persistence (no body-markup, no icons).
  static std::vector<std::string> Capabilities();

  bool IsOpen(uint32_t id) const;
  bool exported() const { return exported_object_ != nullptr; }
  const std::string& server_version() const { return server_version_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // message_center::MessageCenterObserver:
  void OnNotificationRemoved(const std::string& notification_id,
                             bool by_user) override;

  // Called by a notification's delegate when the body (nullopt) or a button
  // was clicked. Public for the delegate; not part of the API.
  void OnClicked(uint32_t id, std::optional<int> button_index);

 private:
  struct OpenNotification {
    OpenNotification();
    OpenNotification(OpenNotification&&);
    OpenNotification& operator=(OpenNotification&&);
    ~OpenNotification();

    std::vector<std::string> button_keys;
    bool has_default_action = false;
    bool resident = false;
    std::unique_ptr<base::OneShotTimer> expiry;
  };

  // D-Bus method handlers (origin thread).
  void HandleGetCapabilities(dbus::MethodCall* method_call,
                             dbus::ExportedObject::ResponseSender sender);
  void HandleNotify(dbus::MethodCall* method_call,
                    dbus::ExportedObject::ResponseSender sender);
  void HandleCloseNotification(dbus::MethodCall* method_call,
                               dbus::ExportedObject::ResponseSender sender);
  void HandleGetServerInformation(dbus::MethodCall* method_call,
                                  dbus::ExportedObject::ResponseSender sender);

  void OnMethodExported(const std::string& interface_name,
                        const std::string& method_name,
                        bool success);
  void OnNameOwned(const std::string& service_name, bool success);

  // Removes |id| from MessageCenter, recording |reason| for the signal.
  void Remove(uint32_t id, CloseReason reason);
  void OnExpired(uint32_t id);
  void InvokeAction(uint32_t id, const std::string& action_key);

  void EmitNotificationClosed(uint32_t id, CloseReason reason);
  void EmitActionInvoked(uint32_t id, const std::string& action_key);

  const raw_ptr<message_center::MessageCenter> message_center_;
  const std::string server_version_;

  scoped_refptr<dbus::Bus> bus_;
  scoped_refptr<dbus::ExportedObject> exported_object_;
  int methods_pending_ = 0;
  bool export_failed_ = false;
  base::OnceCallback<void(bool)> on_exported_;

  uint32_t next_id_ = 1;
  std::map<uint32_t, OpenNotification> open_;
  // The reason the server itself chose for a removal it is making, read by
  // OnNotificationRemoved.
  std::map<uint32_t, CloseReason> pending_close_reason_;

  base::ObserverList<Observer> observers_;
  base::ScopedObservation<message_center::MessageCenter,
                          message_center::MessageCenterObserver>
      message_center_observation_{this};
  base::WeakPtrFactory<NotificationServer> weak_factory_{this};
};

// Maps "views-shell-notification-<n>" back to n; nullopt for other ids.
std::optional<uint32_t> FreedesktopIdFromMessageCenterId(
    const std::string& message_center_id);
std::string MessageCenterIdFor(uint32_t id);

}  // namespace views_shell::notifications

#endif  // VIEWS_SHELL_NOTIFICATIONS_NOTIFICATION_SERVER_H_
