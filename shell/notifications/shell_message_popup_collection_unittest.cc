// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The popup collection with real MessagePopupView widgets, driven through
// NotificationServer's in-process core (no bus): popups appear, stack, close,
// and their action buttons map to freedesktop action keys.
//
// views::ViewsTestBase needs aura::Env, which on this build initializes the
// only Ozone platform it has, Wayland: these tests need a compositor
// (WAYLAND_DISPLAY). The bench runs them inside runtime-test against a
// private headless scroll (tools/bench/seq/w2c.sh); without a display they
// skip and say so. The test hosts are aura test windows, which ignore
// Widget::InitParams::layer_shell, so the layer-surface request is checked as
// data (surface_specs()), and on the bench by the protocol log.
//
// The umbrella's main is //base/test:run_all_unittests, which sets up no UI
// resources; ViewsProcessSetup below does what a Views test suite does, once
// per process.

#include "views_shell/notifications/shell_message_popup_collection.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/time/time.h"
#include "mojo/core/embedder/embedder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/gl/test/gl_surface_test_support.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/message_center/views/message_popup_view.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "views_shell/notifications/notification_server.h"
#include "views_shell/notifications/notification_service.h"

namespace views_shell::notifications {
namespace {

bool HaveWaylandDisplay() {
  auto env = base::Environment::Create();
  return env->HasVar("WAYLAND_DISPLAY") || env->HasVar("WAYLAND_SOCKET");
}

// What views::ViewsTestSuite does before any Views test, done once.
void ViewsProcessSetup() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  mojo::core::Init();
  gl::GLSurfaceTestSupport::InitializeOneOff();
  ui::RegisterPathProvider();
  base::FilePath ui_test_pak;
  CHECK(base::PathService::Get(ui::UI_TEST_PAK, &ui_test_pak));
  ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak);
  static base::NoDestructor<base::TestDiscardableMemoryAllocator> allocator;
  base::DiscardableMemoryAllocator::SetInstance(allocator.get());
}

class RecordingObserver : public NotificationServer::Observer {
 public:
  void OnNotificationClosed(uint32_t id, CloseReason reason) override {
    closed.emplace_back(id, reason);
  }
  void OnActionInvoked(uint32_t id, const std::string& key) override {
    actions.emplace_back(id, key);
  }
  std::vector<std::pair<uint32_t, CloseReason>> closed;
  std::vector<std::pair<uint32_t, std::string>> actions;
};

// The pure part: no widgets, no display.
TEST(ShellMessagePopupSurfaceSpecTest, MarginsPlaceThePopupInItsSlot) {
  const gfx::Rect work_area(0, 0, 1920, 1080);
  const gfx::Rect bounds(1920 - 10 - 360, 10 + 90 + 10, 360, 70);
  const PopupSurfaceSpec spec = PopupSurfaceSpecForBounds(bounds, work_area);
  EXPECT_EQ(ui::LayerShellLayer::kOverlay, spec.layer);
  EXPECT_EQ(ui::kLayerShellAnchorTop | ui::kLayerShellAnchorRight,
            spec.anchor);
  EXPECT_EQ(110, spec.margin_top);
  EXPECT_EQ(10, spec.margin_right);
  EXPECT_EQ(0, spec.margin_bottom);
  EXPECT_EQ(0, spec.margin_left);
  EXPECT_EQ(ui::LayerShellKeyboardInteractivity::kNone, spec.keyboard);
  EXPECT_EQ(0, spec.exclusive_zone);
  EXPECT_EQ("views-shell-notification", spec.layer_namespace);
  EXPECT_EQ(gfx::Size(360, 70), spec.size);

  const ui::LayerShellProperties props = ToLayerShellProperties(spec);
  EXPECT_EQ(ui::LayerShellLayer::kOverlay, props.layer);
  EXPECT_EQ(spec.anchor, props.anchor);
  EXPECT_EQ(110, props.margin_top);
  EXPECT_EQ(10, props.margin_right);
  EXPECT_EQ(ui::LayerShellKeyboardInteractivity::kNone,
            props.keyboard_interactivity);
  EXPECT_EQ("views-shell-notification", props.layer_namespace);
}

class ShellMessagePopupCollectionTest : public views::ViewsTestBase {
 protected:
  ShellMessagePopupCollectionTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    if (!HaveWaylandDisplay()) {
      GTEST_SKIP() << "no WAYLAND_DISPLAY: aura::Env needs a compositor on "
                      "this Wayland-only build; tools/bench/seq/w2c.sh runs "
                      "these tests against a private headless scroll";
    }
    ViewsProcessSetup();
    ax_platform_.emplace();
    views::ViewsTestBase::SetUp();
    message_center::MessageCenter::Initialize();
    popups_ = std::make_unique<ShellMessagePopupCollection>();
    popups_->set_widget_context_for_testing(GetContext());
    popups_->StartObserving();
    server_ = std::make_unique<NotificationServer>(
        message_center::MessageCenter::Get(), "test");
    server_->AddObserver(&observer_);
  }

  void TearDown() override {
    if (!ax_platform_) {
      return;  // Skipped.
    }
    server_->RemoveObserver(&observer_);
    popups_.reset();
    server_.reset();
    message_center::MessageCenter::Shutdown();
    views::ViewsTestBase::TearDown();
    ax_platform_.reset();
  }

  // Lets the fade-in/fade-out and move animations finish.
  void Settle() {
    for (int i = 0; i < 5; ++i) {
      task_environment()->FastForwardBy(base::Seconds(1));
      base::RunLoop().RunUntilIdle();
    }
  }

  uint32_t Notify(const std::string& summary,
                  std::vector<std::string> actions = {}) {
    NotifyParams params;
    params.app_name = "popup-test";
    params.summary = summary;
    params.body = "body";
    params.actions = std::move(actions);
    params.expire_timeout = 0;  // Keep the popups up while the test looks.
    return server_->Notify(params);
  }

  message_center::MessagePopupView* PopupFor(uint32_t id) {
    return popups_->GetPopupViewForNotificationID(MessageCenterIdFor(id));
  }

  gfx::Rect BoundsOf(uint32_t id) {
    message_center::MessagePopupView* popup = PopupFor(id);
    return popup && popup->GetWidget()
               ? popup->GetWidget()->GetWindowBoundsInScreen()
               : gfx::Rect();
  }

  std::optional<ui::AXPlatformForTest> ax_platform_;
  std::unique_ptr<ShellMessagePopupCollection> popups_;
  std::unique_ptr<NotificationServer> server_;
  RecordingObserver observer_;
};

TEST_F(ShellMessagePopupCollectionTest, PopupAppearsOnNotify) {
  const uint32_t id = Notify("one");
  Settle();
  EXPECT_EQ(1u, popups_->GetPopupItemsCount());
  ASSERT_TRUE(PopupFor(id));
  EXPECT_TRUE(PopupFor(id)->GetWidget()->IsVisible());

  ASSERT_EQ(1u, popups_->surface_specs().size());
  const PopupSurfaceSpec& spec = popups_->surface_specs()[0];
  EXPECT_EQ(ui::LayerShellLayer::kOverlay, spec.layer);
  EXPECT_EQ(message_center::kMarginBetweenPopups, spec.margin_top);
  EXPECT_EQ(message_center::kMarginBetweenPopups, spec.margin_right);
  EXPECT_EQ(message_center::GetNotificationWidth(), spec.size.width());
  EXPECT_GT(spec.size.height(), 1);
}

TEST_F(ShellMessagePopupCollectionTest, PopupsStackTopDown) {
  const uint32_t first = Notify("first");
  Settle();
  const uint32_t second = Notify("second");
  Settle();
  EXPECT_EQ(2u, popups_->GetPopupItemsCount());

  const gfx::Rect a = BoundsOf(first);
  const gfx::Rect b = BoundsOf(second);
  ASSERT_FALSE(a.IsEmpty());
  ASSERT_FALSE(b.IsEmpty());
  EXPECT_EQ(a.right(), b.right());
  EXPECT_GE(b.y(), a.bottom());

  // The layer surface of the second asks for the slot below the first.
  ASSERT_EQ(2u, popups_->surface_specs().size());
  const PopupSurfaceSpec& s0 = popups_->surface_specs()[0];
  const PopupSurfaceSpec& s1 = popups_->surface_specs()[1];
  EXPECT_EQ(s0.margin_top + s0.size.height() +
                message_center::kMarginBetweenPopups,
            s1.margin_top);
  EXPECT_EQ(s0.margin_right, s1.margin_right);
}

TEST_F(ShellMessagePopupCollectionTest, CloseNotificationClosesThePopup) {
  const uint32_t keep = Notify("keep");
  const uint32_t close = Notify("close");
  Settle();
  ASSERT_EQ(2u, popups_->GetPopupItemsCount());

  EXPECT_TRUE(server_->CloseNotification(close));
  Settle();
  EXPECT_EQ(1u, popups_->GetPopupItemsCount());
  EXPECT_FALSE(PopupFor(close));
  EXPECT_TRUE(PopupFor(keep));
  ASSERT_EQ(1u, observer_.closed.size());
  EXPECT_EQ(close, observer_.closed[0].first);
  EXPECT_EQ(CloseReason::kClosedByCall, observer_.closed[0].second);
}

TEST_F(ShellMessagePopupCollectionTest, ActionButtonsMapToActionKeys) {
  const uint32_t id =
      Notify("act", {"default", "Open", "archive", "Archive", "snooze",
                     "Snooze"});
  Settle();
  ASSERT_TRUE(PopupFor(id));

  // The popup's notification carries one button per non-default action.
  message_center::Notification* n =
      message_center::MessageCenter::Get()->FindNotificationById(
          MessageCenterIdFor(id));
  ASSERT_TRUE(n);
  ASSERT_EQ(2u, n->buttons().size());
  EXPECT_EQ(u"Archive", n->buttons()[0].title);
  EXPECT_EQ(u"Snooze", n->buttons()[1].title);

  // What the popup's button press calls.
  message_center::MessageCenter::Get()->ClickOnNotificationButton(
      MessageCenterIdFor(id), 1);
  Settle();
  ASSERT_EQ(1u, observer_.actions.size());
  EXPECT_EQ(id, observer_.actions[0].first);
  EXPECT_EQ("snooze", observer_.actions[0].second);
  // The invoked action dismissed the notification and its popup.
  EXPECT_EQ(0u, popups_->GetPopupItemsCount());
  ASSERT_EQ(1u, observer_.closed.size());
  EXPECT_EQ(CloseReason::kDismissedByUser, observer_.closed[0].second);
}

// NotificationService without a bus: it brings MessageCenter and the popups
// up and down; Notify through its server shows a popup.
class NotificationServiceTest : public views::ViewsTestBase {
 protected:
  NotificationServiceTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    if (!HaveWaylandDisplay()) {
      GTEST_SKIP() << "no WAYLAND_DISPLAY: see ShellMessagePopupCollectionTest";
    }
    ViewsProcessSetup();
    ax_platform_.emplace();
    views::ViewsTestBase::SetUp();
  }

  void TearDown() override {
    if (!ax_platform_) {
      return;
    }
    views::ViewsTestBase::TearDown();
    ax_platform_.reset();
  }

  std::optional<ui::AXPlatformForTest> ax_platform_;
};

TEST_F(NotificationServiceTest, StartWithoutBusThenStop) {
  NotificationService service("test");
  std::optional<bool> started;
  service.Start(/*use_session_bus=*/false,
                base::BindLambdaForTesting([&](bool ok) { started = ok; }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(started.has_value());
  EXPECT_FALSE(*started);  // No bus asked for: nothing exported.
  ASSERT_TRUE(message_center::MessageCenter::Get());
  ASSERT_TRUE(service.server());
  EXPECT_FALSE(service.server()->exported());
  service.popups()->set_widget_context_for_testing(GetContext());

  NotifyParams params;
  params.app_name = "service-test";
  params.summary = "hi";
  params.expire_timeout = 0;
  service.server()->Notify(params);
  for (int i = 0; i < 5; ++i) {
    task_environment()->FastForwardBy(base::Seconds(1));
  }
  EXPECT_EQ(1u, service.popups()->GetPopupItemsCount());

  service.Stop();
  EXPECT_FALSE(message_center::MessageCenter::Get());
  EXPECT_FALSE(service.started());
}

}  // namespace
}  // namespace views_shell::notifications
