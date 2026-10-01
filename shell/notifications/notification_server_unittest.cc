// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// NotificationServer without Views: the freedesktop → message_center mapping
// (NotificationServerMappingTest, no bus needed), and the D-Bus interface on a
// real session bus (NotificationServerBusTest). The bus tests need a private
// session bus (run the binary under dbus-run-session); when
// DBUS_SESSION_BUS_ADDRESS is unset they skip and say so, so a run without a
// bus is never green by accident.

#include "views_shell/notifications/notification_server.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/memory/raw_ptr.h"
#include "base/functional/bind.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"

namespace views_shell::notifications {
namespace {

struct Closed {
  uint32_t id;
  CloseReason reason;
};

class RecordingObserver : public NotificationServer::Observer {
 public:
  void OnNotificationClosed(uint32_t id, CloseReason reason) override {
    closed.push_back({id, reason});
  }
  void OnActionInvoked(uint32_t id, const std::string& key) override {
    actions.emplace_back(id, key);
  }

  std::vector<Closed> closed;
  std::vector<std::pair<uint32_t, std::string>> actions;
};

NotifyParams Params(const std::string& summary) {
  NotifyParams params;
  params.app_name = "test-app";
  params.summary = summary;
  params.body = "body of " + summary;
  return params;
}

// ---------------------------------------------------------------------------
// The mapping, in-process.

class NotificationServerMappingTest : public testing::Test {
 protected:
  void SetUp() override {
    message_center::MessageCenter::Initialize();
    server_ = std::make_unique<NotificationServer>(
        message_center::MessageCenter::Get(), "test-version");
    server_->AddObserver(&observer_);
  }
  void TearDown() override {
    server_->RemoveObserver(&observer_);
    server_.reset();
    message_center::MessageCenter::Shutdown();
  }

  message_center::Notification* Find(uint32_t id) {
    return message_center::MessageCenter::Get()->FindNotificationById(
        MessageCenterIdFor(id));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<NotificationServer> server_;
  RecordingObserver observer_;
};

TEST_F(NotificationServerMappingTest, NotifyAddsASimpleNotification) {
  NotifyParams params = Params("hello");
  const uint32_t id = server_->Notify(params);
  EXPECT_NE(0u, id);
  message_center::Notification* n = Find(id);
  ASSERT_TRUE(n);
  EXPECT_EQ(message_center::NOTIFICATION_TYPE_SIMPLE, n->type());
  EXPECT_EQ(u"hello", n->title());
  EXPECT_EQ(u"body of hello", n->message());
  EXPECT_EQ(u"test-app", n->display_source());
  EXPECT_EQ("test-app", n->notifier_id().id);
  EXPECT_EQ(message_center::NotifierType::APPLICATION, n->notifier_id().type);
  EXPECT_EQ(message_center::DEFAULT_PRIORITY, n->priority());
  EXPECT_FALSE(n->never_timeout());
}

TEST_F(NotificationServerMappingTest, ReplacesIdKeepsTheId) {
  const uint32_t id = server_->Notify(Params("first"));
  NotifyParams update = Params("second");
  update.replaces_id = id;
  EXPECT_EQ(id, server_->Notify(update));
  EXPECT_EQ(1u, message_center::MessageCenter::Get()->NotificationCount());
  EXPECT_EQ(u"second", Find(id)->title());
  EXPECT_TRUE(observer_.closed.empty());

  // A replaces_id that is not open gets a new id.
  NotifyParams stale = Params("third");
  stale.replaces_id = id + 100;
  const uint32_t other = server_->Notify(stale);
  EXPECT_NE(id, other);
  EXPECT_NE(id + 100, other);
}

TEST_F(NotificationServerMappingTest, UrgencyMapsToPriority) {
  NotifyParams low = Params("low");
  low.urgency = Urgency::kLow;
  NotifyParams critical = Params("critical");
  critical.urgency = Urgency::kCritical;
  EXPECT_EQ(message_center::DEFAULT_PRIORITY,
            Find(server_->Notify(low))->priority());
  message_center::Notification* n = Find(server_->Notify(critical));
  EXPECT_EQ(message_center::SYSTEM_PRIORITY, n->priority());
  EXPECT_TRUE(n->never_timeout());
}

TEST_F(NotificationServerMappingTest, ActionsBecomeButtons) {
  NotifyParams params = Params("actions");
  params.actions = {"default", "Open", "reply", "Reply", "later", "Later",
                    "dangling"};
  const uint32_t id = server_->Notify(params);
  message_center::Notification* n = Find(id);
  ASSERT_EQ(2u, n->buttons().size());
  EXPECT_EQ(u"Reply", n->buttons()[0].title);
  EXPECT_EQ(u"Later", n->buttons()[1].title);
  EXPECT_EQ((std::vector<std::string>{"reply", "later"}),
            ButtonActionKeys(params.actions));
}

TEST_F(NotificationServerMappingTest, ExpireTimeoutIsRespected) {
  NotifyParams timed = Params("timed");
  timed.expire_timeout = 1500;
  NotifyParams never = Params("never");
  never.expire_timeout = 0;
  NotifyParams server_default = Params("default");  // -1
  const uint32_t timed_id = server_->Notify(timed);
  const uint32_t never_id = server_->Notify(never);
  const uint32_t default_id = server_->Notify(server_default);
  EXPECT_TRUE(Find(never_id)->never_timeout());

  task_environment_.FastForwardBy(base::Milliseconds(1400));
  EXPECT_TRUE(Find(timed_id));
  task_environment_.FastForwardBy(base::Milliseconds(200));
  EXPECT_FALSE(Find(timed_id));
  ASSERT_EQ(1u, observer_.closed.size());
  EXPECT_EQ(timed_id, observer_.closed[0].id);
  EXPECT_EQ(CloseReason::kExpired, observer_.closed[0].reason);

  // 0 and -1 are kept (persistence) long after.
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_TRUE(Find(never_id));
  EXPECT_TRUE(Find(default_id));
}

TEST_F(NotificationServerMappingTest, CloseReasons) {
  const uint32_t by_call = server_->Notify(Params("a"));
  const uint32_t by_user = server_->Notify(Params("b"));
  EXPECT_TRUE(server_->CloseNotification(by_call));
  EXPECT_FALSE(server_->CloseNotification(by_call));
  message_center::MessageCenter::Get()->RemoveNotification(
      MessageCenterIdFor(by_user), /*by_user=*/true);
  ASSERT_EQ(2u, observer_.closed.size());
  EXPECT_EQ(CloseReason::kClosedByCall, observer_.closed[0].reason);
  EXPECT_EQ(by_user, observer_.closed[1].id);
  EXPECT_EQ(CloseReason::kDismissedByUser, observer_.closed[1].reason);
}

TEST_F(NotificationServerMappingTest, ClicksInvokeActionKeys) {
  NotifyParams params = Params("click");
  params.actions = {"default", "Open", "reply", "Reply", "later", "Later"};
  const uint32_t id = server_->Notify(params);
  auto* center = message_center::MessageCenter::Get();
  center->ClickOnNotificationButton(MessageCenterIdFor(id), 1);
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, observer_.actions.size());
  EXPECT_EQ(id, observer_.actions[0].first);
  EXPECT_EQ("later", observer_.actions[0].second);
  // Not resident: the action dismissed it.
  ASSERT_EQ(1u, observer_.closed.size());
  EXPECT_EQ(CloseReason::kDismissedByUser, observer_.closed[0].reason);

  params.resident = true;
  const uint32_t resident = server_->Notify(params);
  center->ClickOnNotification(MessageCenterIdFor(resident));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(2u, observer_.actions.size());
  EXPECT_EQ("default", observer_.actions[1].second);
  EXPECT_TRUE(Find(resident));
}

TEST_F(NotificationServerMappingTest, IdsRoundTrip) {
  EXPECT_EQ(42u, FreedesktopIdFromMessageCenterId(MessageCenterIdFor(42)));
  EXPECT_FALSE(FreedesktopIdFromMessageCenterId("other-42"));
  EXPECT_FALSE(FreedesktopIdFromMessageCenterId(MessageCenterIdFor(0)));
}

// ---------------------------------------------------------------------------
// The D-Bus interface on a real (private) session bus.

class NotificationServerBusTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!base::Environment::Create()->HasVar("DBUS_SESSION_BUS_ADDRESS")) {
      GTEST_SKIP() << "DBUS_SESSION_BUS_ADDRESS is unset: the D-Bus tests "
                      "need a private session bus; run the binary under "
                      "dbus-run-session";
    }
    message_center::MessageCenter::Initialize();

    dbus_thread_ = std::make_unique<base::Thread>("test D-Bus");
    base::Thread::Options thread_options;
    thread_options.message_pump_type = base::MessagePumpType::IO;
    ASSERT_TRUE(dbus_thread_->StartWithOptions(std::move(thread_options)));

    server_bus_ = MakeBus();
    client_bus_ = MakeBus();
    server_ = std::make_unique<NotificationServer>(
        message_center::MessageCenter::Get(), "abc1234");
    base::test::TestFuture<bool> exported;
    server_->Export(server_bus_, exported.GetCallback());
    ASSERT_TRUE(exported.Get()) << "could not own " << kNotificationsServiceName;

    proxy_ = client_bus_->GetObjectProxy(
        kNotificationsServiceName, dbus::ObjectPath(kNotificationsObjectPath));
  }

  void TearDown() override {
    if (!server_) {
      return;
    }
    server_->Unexport();
    client_bus_->ShutdownOnDBusThreadAndBlock();
    server_bus_->ShutdownOnDBusThreadAndBlock();
    dbus_thread_->Stop();
    server_.reset();
    message_center::MessageCenter::Shutdown();
  }

  scoped_refptr<dbus::Bus> MakeBus() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SESSION;
    options.connection_type = dbus::Bus::PRIVATE;
    options.dbus_task_runner = dbus_thread_->task_runner();
    return base::MakeRefCounted<dbus::Bus>(std::move(options));
  }

  // Calls |method_call| and returns the response, or nullptr on an error.
  std::unique_ptr<dbus::Response> Call(dbus::MethodCall* method_call) {
    base::RunLoop run_loop;
    std::unique_ptr<dbus::Response> result;
    proxy_->CallMethod(
        method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindLambdaForTesting([&](dbus::Response* response) {
          if (response) {
            result = dbus::Response::FromRawMessage(
                dbus_message_copy(response->raw_message()));
          }
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

  uint32_t CallNotify(const std::string& summary,
                      const std::vector<std::string>& actions) {
    dbus::MethodCall call(kNotificationsInterface, "Notify");
    dbus::MessageWriter writer(&call);
    writer.AppendString("bus-app");
    writer.AppendUint32(0);
    writer.AppendString("");
    writer.AppendString(summary);
    writer.AppendString("over D-Bus");
    writer.AppendArrayOfStrings(actions);
    dbus::MessageWriter hints(nullptr);
    writer.OpenArray("{sv}", &hints);
    dbus::MessageWriter entry(nullptr);
    hints.OpenDictEntry(&entry);
    entry.AppendString("urgency");
    entry.AppendVariantOfByte(2);
    hints.CloseContainer(&entry);
    writer.CloseContainer(&hints);
    writer.AppendInt32(-1);
    std::unique_ptr<dbus::Response> response = Call(&call);
    EXPECT_TRUE(response);
    uint32_t id = 0;
    if (response) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopUint32(&id));
    }
    return id;
  }

  // Connects to |signal| and returns once the match rule is installed.
  void Listen(const std::string& signal,
              base::RepeatingCallback<void(dbus::Signal*)> on_signal) {
    base::test::TestFuture<std::string, std::string, bool> connected;
    proxy_->ConnectToSignal(
        kNotificationsInterface, signal, std::move(on_signal),
        connected.GetCallback<const std::string&, const std::string&, bool>());
    ASSERT_TRUE(connected.Get<2>());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  std::unique_ptr<base::Thread> dbus_thread_;
  scoped_refptr<dbus::Bus> server_bus_;
  scoped_refptr<dbus::Bus> client_bus_;
  std::unique_ptr<NotificationServer> server_;
  raw_ptr<dbus::ObjectProxy> proxy_ = nullptr;
};

TEST_F(NotificationServerBusTest, ServerInformationAndCapabilities) {
  dbus::MethodCall info(kNotificationsInterface, "GetServerInformation");
  std::unique_ptr<dbus::Response> response = Call(&info);
  ASSERT_TRUE(response);
  dbus::MessageReader reader(response.get());
  std::string name, vendor, version, spec;
  ASSERT_TRUE(reader.PopString(&name));
  ASSERT_TRUE(reader.PopString(&vendor));
  ASSERT_TRUE(reader.PopString(&version));
  ASSERT_TRUE(reader.PopString(&spec));
  EXPECT_EQ("views-shell", name);
  EXPECT_EQ("abc1234", version);
  EXPECT_EQ("1.2", spec);

  dbus::MethodCall caps(kNotificationsInterface, "GetCapabilities");
  response = Call(&caps);
  ASSERT_TRUE(response);
  dbus::MessageReader caps_reader(response.get());
  std::vector<std::string> capabilities;
  ASSERT_TRUE(caps_reader.PopArrayOfStrings(&capabilities));
  EXPECT_EQ((std::vector<std::string>{"body", "actions", "persistence"}),
            capabilities);
}

TEST_F(NotificationServerBusTest, NotifyReachesMessageCenter) {
  const uint32_t id = CallNotify("from the bus", {"default", "Open"});
  ASSERT_NE(0u, id);
  message_center::Notification* n =
      message_center::MessageCenter::Get()->FindNotificationById(
          MessageCenterIdFor(id));
  ASSERT_TRUE(n);
  EXPECT_EQ(u"from the bus", n->title());
  EXPECT_EQ(u"over D-Bus", n->message());
  EXPECT_EQ(message_center::SYSTEM_PRIORITY, n->priority());  // urgency 2

  // A malformed Notify is an InvalidArgs error, not a crash.
  dbus::MethodCall bad(kNotificationsInterface, "Notify");
  dbus::MessageWriter(&bad).AppendString("only an app name");
  EXPECT_FALSE(Call(&bad));
}

TEST_F(NotificationServerBusTest, CloseEmitsNotificationClosed) {
  std::optional<std::pair<uint32_t, uint32_t>> closed;
  base::RunLoop signalled;
  Listen("NotificationClosed",
         base::BindLambdaForTesting([&](dbus::Signal* signal) {
           dbus::MessageReader reader(signal);
           uint32_t id = 0, reason = 0;
           ASSERT_TRUE(reader.PopUint32(&id));
           ASSERT_TRUE(reader.PopUint32(&reason));
           closed.emplace(id, reason);
           signalled.Quit();
         }));
  const uint32_t id = CallNotify("to close", {});
  dbus::MethodCall close(kNotificationsInterface, "CloseNotification");
  dbus::MessageWriter(&close).AppendUint32(id);
  ASSERT_TRUE(Call(&close));
  signalled.Run();
  ASSERT_TRUE(closed);
  EXPECT_EQ(id, closed->first);
  EXPECT_EQ(3u, closed->second);  // closed by a call to CloseNotification
  EXPECT_FALSE(message_center::MessageCenter::Get()->FindNotificationById(
      MessageCenterIdFor(id)));
}

TEST_F(NotificationServerBusTest, ButtonEmitsActionInvoked) {
  std::optional<std::pair<uint32_t, std::string>> invoked;
  base::RunLoop signalled;
  Listen("ActionInvoked",
         base::BindLambdaForTesting([&](dbus::Signal* signal) {
           dbus::MessageReader reader(signal);
           uint32_t id = 0;
           std::string key;
           ASSERT_TRUE(reader.PopUint32(&id));
           ASSERT_TRUE(reader.PopString(&key));
           invoked.emplace(id, key);
           signalled.Quit();
         }));
  const uint32_t id = CallNotify("act", {"open", "Open", "mute", "Mute"});
  message_center::MessageCenter::Get()->ClickOnNotificationButton(
      MessageCenterIdFor(id), 1);
  signalled.Run();
  ASSERT_TRUE(invoked);
  EXPECT_EQ(id, invoked->first);
  EXPECT_EQ("mute", invoked->second);
}

}  // namespace
}  // namespace views_shell::notifications
