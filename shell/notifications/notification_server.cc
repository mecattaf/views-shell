// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/notifications/notification_server.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "ui/base/models/image_model.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_delegate.h"
#include "ui/message_center/public/cpp/notification_types.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace views_shell::notifications {

namespace {

constexpr char kDefaultActionKey[] = "default";
constexpr char kErrorInvalidArgs[] = "org.freedesktop.DBus.Error.InvalidArgs";

constexpr const char* kMethods[] = {
    "GetCapabilities",
    "Notify",
    "CloseNotification",
    "GetServerInformation",
};

// Forwards clicks on one notification to the server. The server may be gone
// (the delegate is ref-counted and can outlive it), hence the WeakPtr.
class ClickForwarder : public message_center::NotificationDelegate {
 public:
  ClickForwarder(base::WeakPtr<NotificationServer> server, uint32_t id)
      : server_(std::move(server)), id_(id) {}
  ClickForwarder(const ClickForwarder&) = delete;
  ClickForwarder& operator=(const ClickForwarder&) = delete;

  // message_center::NotificationDelegate:
  void Click(const std::optional<int>& button_index,
             const std::optional<std::u16string>& reply) override {
    if (server_) {
      server_->OnClicked(id_, button_index);
    }
  }

 private:
  ~ClickForwarder() override = default;

  base::WeakPtr<NotificationServer> server_;
  const uint32_t id_;
};

// Reads one hint value. Returns false only on a malformed variant; an
// unexpected type for a known hint is ignored, like an unknown hint.
bool ReadHint(const std::string& key,
              dbus::MessageReader* variant,
              NotifyParams* params) {
  const dbus::Message::DataType type = variant->GetDataType();
  if (key == "urgency") {
    uint8_t byte = 0;
    if (type == dbus::Message::BYTE && variant->PopByte(&byte) && byte <= 2) {
      params->urgency = static_cast<Urgency>(byte);
    }
    return true;
  }
  if (key == "resident" || key == "transient") {
    bool value = false;
    if (type == dbus::Message::BOOL && variant->PopBool(&value)) {
      (key == "resident" ? params->resident : params->transient) = value;
    }
    return true;
  }
  if (key == "category" || key == "desktop-entry") {
    std::string value;
    if (type == dbus::Message::STRING && variant->PopString(&value)) {
      (key == "category" ? params->category : params->desktop_entry) =
          std::move(value);
    }
    return true;
  }
  return true;
}

bool ReadNotifyParams(dbus::MethodCall* method_call, NotifyParams* params) {
  dbus::MessageReader reader(method_call);
  dbus::MessageReader hints(nullptr);
  if (!reader.PopString(&params->app_name) ||
      !reader.PopUint32(&params->replaces_id) ||
      !reader.PopString(&params->app_icon) ||
      !reader.PopString(&params->summary) ||
      !reader.PopString(&params->body) ||
      !reader.PopArrayOfStrings(&params->actions) ||
      !reader.PopArray(&hints)) {
    return false;
  }
  while (hints.HasMoreData()) {
    dbus::MessageReader entry(nullptr);
    std::string key;
    dbus::MessageReader variant(nullptr);
    if (!hints.PopDictEntry(&entry) || !entry.PopString(&key) ||
        !entry.PopVariant(&variant)) {
      return false;
    }
    if (!ReadHint(key, &variant, params)) {
      return false;
    }
  }
  return reader.PopInt32(&params->expire_timeout) && !reader.HasMoreData();
}

}  // namespace

NotifyParams::NotifyParams() = default;
NotifyParams::NotifyParams(const NotifyParams&) = default;
NotifyParams& NotifyParams::operator=(const NotifyParams&) = default;
NotifyParams::~NotifyParams() = default;

std::string MessageCenterIdFor(uint32_t id) {
  return kMessageCenterIdPrefix + base::NumberToString(id);
}

std::optional<uint32_t> FreedesktopIdFromMessageCenterId(
    const std::string& message_center_id) {
  const std::string_view prefix(kMessageCenterIdPrefix);
  if (!std::string_view(message_center_id).starts_with(prefix)) {
    return std::nullopt;
  }
  uint32_t id = 0;
  if (!base::StringToUint(
          std::string_view(message_center_id).substr(prefix.size()), &id) ||
      id == 0) {
    return std::nullopt;
  }
  return id;
}

int PriorityForUrgency(Urgency urgency) {
  switch (urgency) {
    case Urgency::kLow:
    case Urgency::kNormal:
      return message_center::DEFAULT_PRIORITY;
    case Urgency::kCritical:
      return message_center::SYSTEM_PRIORITY;
  }
  return message_center::DEFAULT_PRIORITY;
}

std::vector<std::string> ButtonActionKeys(
    const std::vector<std::string>& actions) {
  std::vector<std::string> keys;
  for (size_t i = 0; i + 1 < actions.size(); i += 2) {
    if (actions[i] != kDefaultActionKey) {
      keys.push_back(actions[i]);
    }
  }
  return keys;
}

std::unique_ptr<message_center::Notification> BuildMessageCenterNotification(
    uint32_t id,
    const NotifyParams& params,
    scoped_refptr<message_center::NotificationDelegate> delegate) {
  message_center::RichNotificationData data;
  data.priority = PriorityForUrgency(params.urgency);
  // Critical notifications do not expire (specification 1.2, "Urgency
  // Levels"); 0 asks for the same.
  data.never_timeout = params.urgency == Urgency::kCritical ||
                       params.expire_timeout == 0;
  for (size_t i = 0; i + 1 < params.actions.size(); i += 2) {
    if (params.actions[i] == kDefaultActionKey) {
      continue;
    }
    data.buttons.emplace_back(base::UTF8ToUTF16(params.actions[i + 1]));
  }
  const std::u16string app_name = base::UTF8ToUTF16(params.app_name);
  return std::make_unique<message_center::Notification>(
      message_center::NOTIFICATION_TYPE_SIMPLE, MessageCenterIdFor(id),
      base::UTF8ToUTF16(params.summary), base::UTF8ToUTF16(params.body),
      ui::ImageModel(), app_name, GURL(),
      message_center::NotifierId(message_center::NotifierType::APPLICATION,
                                 params.app_name),
      data, std::move(delegate));
}

NotificationServer::OpenNotification::OpenNotification() = default;
NotificationServer::OpenNotification::OpenNotification(OpenNotification&&) =
    default;
NotificationServer::OpenNotification&
NotificationServer::OpenNotification::operator=(OpenNotification&&) = default;
NotificationServer::OpenNotification::~OpenNotification() = default;

NotificationServer::NotificationServer(
    message_center::MessageCenter* message_center,
    std::string server_version)
    : message_center_(message_center),
      server_version_(std::move(server_version)) {
  CHECK(message_center_);
  message_center_observation_.Observe(message_center_.get());
}

NotificationServer::~NotificationServer() {
  Unexport();
}

// static
std::vector<std::string> NotificationServer::Capabilities() {
  return {"body", "actions", "persistence"};
}

void NotificationServer::Export(scoped_refptr<dbus::Bus> bus,
                                base::OnceCallback<void(bool)> on_done) {
  CHECK(bus);
  CHECK(!bus_) << "NotificationServer::Export is called once";
  bus_ = std::move(bus);
  on_exported_ = std::move(on_done);
  exported_object_ =
      bus_->GetExportedObject(dbus::ObjectPath(kNotificationsObjectPath));
  using Handler = void (NotificationServer::*)(
      dbus::MethodCall*, dbus::ExportedObject::ResponseSender);
  constexpr Handler kHandlers[] = {
      &NotificationServer::HandleGetCapabilities,
      &NotificationServer::HandleNotify,
      &NotificationServer::HandleCloseNotification,
      &NotificationServer::HandleGetServerInformation,
  };
  methods_pending_ = std::size(kMethods);
  for (size_t i = 0; i < std::size(kMethods); ++i) {
    exported_object_->ExportMethod(
        kNotificationsInterface, kMethods[i],
        base::BindRepeating(kHandlers[i], weak_factory_.GetWeakPtr()),
        base::BindOnce(&NotificationServer::OnMethodExported,
                       weak_factory_.GetWeakPtr()));
  }
}

void NotificationServer::OnMethodExported(const std::string& interface_name,
                                          const std::string& method_name,
                                          bool success) {
  if (!success) {
    LOG(ERROR) << "notifications: exporting " << interface_name << "."
               << method_name << " failed";
    export_failed_ = true;
  }
  if (--methods_pending_ > 0) {
    return;
  }
  if (export_failed_) {
    if (on_exported_) {
      std::move(on_exported_).Run(false);
    }
    return;
  }
  // Methods first, then the name (dbus/exported_object.h explains the race).
  bus_->RequestOwnership(
      kNotificationsServiceName, dbus::Bus::REQUIRE_PRIMARY,
      base::BindOnce(&NotificationServer::OnNameOwned,
                     weak_factory_.GetWeakPtr()));
}

void NotificationServer::OnNameOwned(const std::string& service_name,
                                     bool success) {
  if (!success) {
    LOG(ERROR) << "notifications: could not own " << service_name
               << " (another notification daemon runs on this session bus)";
  }
  if (on_exported_) {
    std::move(on_exported_).Run(success);
  }
}

void NotificationServer::Unexport() {
  if (!bus_) {
    return;
  }
  if (exported_object_) {
    bus_->UnregisterExportedObject(dbus::ObjectPath(kNotificationsObjectPath));
    exported_object_ = nullptr;
  }
  bus_ = nullptr;
}

uint32_t NotificationServer::Notify(const NotifyParams& params) {
  uint32_t id = 0;
  const bool replace = params.replaces_id != 0 && IsOpen(params.replaces_id);
  if (replace) {
    id = params.replaces_id;
  } else {
    id = next_id_++;
    if (next_id_ == 0) {  // uint32 wrap: 0 is never an id.
      next_id_ = 1;
    }
  }

  OpenNotification& open = open_[id];
  open.button_keys = ButtonActionKeys(params.actions);
  open.has_default_action = false;
  for (size_t i = 0; i + 1 < params.actions.size(); i += 2) {
    if (params.actions[i] == kDefaultActionKey) {
      open.has_default_action = true;
    }
  }
  open.resident = params.resident;
  open.expiry.reset();

  auto notification = BuildMessageCenterNotification(
      id, params,
      base::MakeRefCounted<ClickForwarder>(weak_factory_.GetWeakPtr(), id));
  if (replace) {
    message_center_->UpdateNotification(MessageCenterIdFor(id),
                                        std::move(notification));
  } else {
    message_center_->AddNotification(std::move(notification));
  }

  // An explicit timeout removes the notification itself, so it also leaves
  // MessageCenter (persistence covers only the server's default, -1).
  if (params.expire_timeout > 0 && params.urgency != Urgency::kCritical) {
    open.expiry = std::make_unique<base::OneShotTimer>();
    open.expiry->Start(FROM_HERE, base::Milliseconds(params.expire_timeout),
                       base::BindOnce(&NotificationServer::OnExpired,
                                      weak_factory_.GetWeakPtr(), id));
  }
  return id;
}

bool NotificationServer::CloseNotification(uint32_t id) {
  if (!IsOpen(id)) {
    return false;
  }
  Remove(id, CloseReason::kClosedByCall);
  return true;
}

bool NotificationServer::IsOpen(uint32_t id) const {
  return open_.contains(id);
}

void NotificationServer::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void NotificationServer::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void NotificationServer::Remove(uint32_t id, CloseReason reason) {
  pending_close_reason_[id] = reason;
  message_center_->RemoveNotification(MessageCenterIdFor(id),
                                      reason == CloseReason::kDismissedByUser);
}

void NotificationServer::OnExpired(uint32_t id) {
  if (IsOpen(id)) {
    Remove(id, CloseReason::kExpired);
  }
}

void NotificationServer::OnNotificationRemoved(
    const std::string& notification_id,
    bool by_user) {
  const std::optional<uint32_t> id =
      FreedesktopIdFromMessageCenterId(notification_id);
  if (!id || !IsOpen(*id)) {
    return;
  }
  CloseReason reason =
      by_user ? CloseReason::kDismissedByUser : CloseReason::kUndefined;
  auto pending = pending_close_reason_.find(*id);
  if (pending != pending_close_reason_.end()) {
    reason = pending->second;
    pending_close_reason_.erase(pending);
  }
  open_.erase(*id);
  EmitNotificationClosed(*id, reason);
}

void NotificationServer::OnClicked(uint32_t id,
                                   std::optional<int> button_index) {
  auto it = open_.find(id);
  if (it == open_.end()) {
    return;
  }
  std::string key;
  if (button_index.has_value()) {
    const int index = *button_index;
    if (index < 0 ||
        static_cast<size_t>(index) >= it->second.button_keys.size()) {
      return;
    }
    key = it->second.button_keys[index];
  } else if (it->second.has_default_action) {
    key = kDefaultActionKey;
  } else {
    return;
  }
  // MessageCenter is inside its click dispatch; act after it returns.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NotificationServer::InvokeAction,
                                weak_factory_.GetWeakPtr(), id, key));
}

void NotificationServer::InvokeAction(uint32_t id,
                                      const std::string& action_key) {
  auto it = open_.find(id);
  if (it == open_.end()) {
    return;
  }
  const bool resident = it->second.resident;
  EmitActionInvoked(id, action_key);
  // An invoked action dismisses the notification unless it is resident
  // (specification 1.2, hint "resident").
  if (!resident) {
    Remove(id, CloseReason::kDismissedByUser);
  }
}

void NotificationServer::EmitNotificationClosed(uint32_t id,
                                                CloseReason reason) {
  if (exported_object_) {
    dbus::Signal signal(kNotificationsInterface, "NotificationClosed");
    dbus::MessageWriter writer(&signal);
    writer.AppendUint32(id);
    writer.AppendUint32(static_cast<uint32_t>(reason));
    exported_object_->SendSignal(&signal);
  }
  for (Observer& observer : observers_) {
    observer.OnNotificationClosed(id, reason);
  }
}

void NotificationServer::EmitActionInvoked(uint32_t id,
                                           const std::string& action_key) {
  if (exported_object_) {
    dbus::Signal signal(kNotificationsInterface, "ActionInvoked");
    dbus::MessageWriter writer(&signal);
    writer.AppendUint32(id);
    writer.AppendString(action_key);
    exported_object_->SendSignal(&signal);
  }
  for (Observer& observer : observers_) {
    observer.OnActionInvoked(id, action_key);
  }
}

void NotificationServer::HandleGetCapabilities(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendArrayOfStrings(Capabilities());
  std::move(sender).Run(std::move(response));
}

void NotificationServer::HandleNotify(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  NotifyParams params;
  if (!ReadNotifyParams(method_call, &params)) {
    std::move(sender).Run(dbus::ErrorResponse::FromMethodCall(
        method_call, kErrorInvalidArgs,
        "Notify takes (susssasa{sv}i)"));
    return;
  }
  const uint32_t id = Notify(params);
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendUint32(id);
  std::move(sender).Run(std::move(response));
}

void NotificationServer::HandleCloseNotification(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  dbus::MessageReader reader(method_call);
  uint32_t id = 0;
  if (!reader.PopUint32(&id) || reader.HasMoreData()) {
    std::move(sender).Run(dbus::ErrorResponse::FromMethodCall(
        method_call, kErrorInvalidArgs, "CloseNotification takes (u)"));
    return;
  }
  // An id that is not open gets an empty reply, as most servers do; the
  // specification's "empty D-Bus error" has no error name to send.
  CloseNotification(id);
  std::move(sender).Run(dbus::Response::FromMethodCall(method_call));
}

void NotificationServer::HandleGetServerInformation(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendString(kServerName);
  writer.AppendString(kServerVendor);
  writer.AppendString(server_version_);
  writer.AppendString(kNotificationsSpecVersion);
  std::move(sender).Run(std::move(response));
}

}  // namespace views_shell::notifications
