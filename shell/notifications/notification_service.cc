// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/notifications/notification_service.h"

#include <utility>

#include "base/check.h"
#include "base/environment.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "dbus/bus.h"
#include "ui/message_center/message_center.h"
#include "views_shell/notifications/notification_server.h"
#include "views_shell/notifications/shell_message_popup_collection.h"

namespace views_shell::notifications {

NotificationService::NotificationService(std::string server_version)
    : server_version_(std::move(server_version)) {}

NotificationService::~NotificationService() {
  Stop();
}

void NotificationService::Start(bool use_session_bus,
                                base::OnceCallback<void(bool)> on_started) {
  CHECK(!started_) << "NotificationService::Start is called once";
  started_ = true;
  on_started_ = std::move(on_started);

  if (!message_center::MessageCenter::Get()) {
    message_center::MessageCenter::Initialize();
    owns_message_center_ = true;
  }
  popups_ = std::make_unique<ShellMessagePopupCollection>();
  popups_->StartObserving();
  server_ = std::make_unique<NotificationServer>(
      message_center::MessageCenter::Get(), server_version_);

  const bool have_address = base::Environment::Create()->HasVar(
      "DBUS_SESSION_BUS_ADDRESS");
  if (!use_session_bus || !have_address) {
    if (use_session_bus) {
      LOG(WARNING) << "notifications: DBUS_SESSION_BUS_ADDRESS is unset; "
                      "org.freedesktop.Notifications is not served";
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&NotificationService::OnExported,
                                  weak_factory_.GetWeakPtr(), false));
    return;
  }

  dbus_thread_ = std::make_unique<base::Thread>("views-shell D-Bus");
  base::Thread::Options thread_options;
  thread_options.message_pump_type = base::MessagePumpType::IO;
  CHECK(dbus_thread_->StartWithOptions(std::move(thread_options)));

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SESSION;
  options.connection_type = dbus::Bus::PRIVATE;
  options.dbus_task_runner = dbus_thread_->task_runner();
  bus_ = base::MakeRefCounted<dbus::Bus>(std::move(options));
  server_->Export(bus_, base::BindOnce(&NotificationService::OnExported,
                                       weak_factory_.GetWeakPtr()));
}

void NotificationService::OnExported(bool success) {
  if (on_started_) {
    std::move(on_started_).Run(success);
  }
}

void NotificationService::Stop() {
  if (!started_) {
    return;
  }
  started_ = false;
  weak_factory_.InvalidateWeakPtrs();
  on_started_.Reset();

  if (server_) {
    server_->Unexport();
  }
  if (bus_) {
    bus_->ShutdownOnDBusThreadAndBlock();
    bus_ = nullptr;
  }
  if (dbus_thread_) {
    dbus_thread_->Stop();
    dbus_thread_.reset();
  }
  // The popups and the server observe MessageCenter: both go before it.
  popups_.reset();
  server_.reset();
  if (owns_message_center_) {
    message_center::MessageCenter::Shutdown();
    owns_message_center_ = false;
  }
}

}  // namespace views_shell::notifications
