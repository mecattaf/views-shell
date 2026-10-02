// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/tabs/workspace_strip_model_binding.h"

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
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "mojo/core/embedder/embedder.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gl/test/gl_surface_test_support.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "views_shell/style/layout_provider.h"
#include "views_shell/tabs/tab.h"
#include "views_shell/tabs/workspace_strip.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/wm/wm_model.h"
#include "views_shell/wm/wm_snapshot.h"

namespace views_shell {
namespace {

class FakeAdapter : public CompositorAdapter {
 public:
  std::string_view name() const override { return "fake"; }
  const CapabilitySet& capabilities() const override { return capabilities_; }
  void Start(Delegate* delegate) override {}
  void Send(const WmCommand& command, CommandDone done) override {
    sent.push_back(command);
    pending.push_back(std::move(done));
  }

  std::vector<WmCommand> sent;
  std::vector<CommandDone> pending;

 private:
  CapabilitySet capabilities_{"workspaces.focus"};
};

struct WorkspaceSpec {
  std::string id;
  std::string name;
  bool focused = false;
};

WmSnapshot MakeSnapshot(const std::vector<WorkspaceSpec>& specs) {
  WmSnapshot snapshot;
  WmOutput output;
  output.id = "HEADLESS-1";
  output.focused = true;
  snapshot.outputs.push_back(output);
  int index = 0;
  for (const WorkspaceSpec& spec : specs) {
    WmWorkspace workspace;
    workspace.id = spec.id;
    workspace.name = spec.name;
    workspace.index = index++;
    workspace.output = "HEADLESS-1";
    workspace.focused = spec.focused;
    workspace.active = spec.focused;
    snapshot.workspaces.push_back(std::move(workspace));
  }
  return snapshot;
}

// The titles, in order, with '*' after the active one.
std::string Describe(const WorkspaceStrip& strip) {
  std::string out;
  int i = 0;
  for (const Workspace& workspace : strip.workspaces()) {
    if (!out.empty()) {
      out += " ";
    }
    out += base::UTF16ToUTF8(workspace.title);
    if (i++ == strip.active_index()) {
      out += "*";
    }
  }
  return out;
}

bool HaveWaylandDisplay() {
  auto env = base::Environment::Create();
  return env->HasVar("WAYLAND_DISPLAY") || env->HasVar("WAYLAND_SOCKET");
}

// What views::ViewsTestSuite does before any Views test, done once (as in
// notifications/shell_message_popup_collection_unittest.cc).
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

// views::ViewsTestBase, held rather than inherited: its destructor CHECKs
// that SetUp ran, so a fixture that skips (no display) cannot be one.
class ViewsHarness : public views::ViewsTestBase {
 public:
  ViewsHarness() = default;
  void SetUp() override { views::ViewsTestBase::SetUp(); }
  void TearDown() override { views::ViewsTestBase::TearDown(); }
  std::unique_ptr<views::Widget> MakeWidget() {
    return CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  }

 private:
  void TestBody() override {}
};

constexpr char kNoDisplay[] =
    "no WAYLAND_DISPLAY: aura::Env needs a compositor on this Wayland-only "
    "build; the bench runs these tests against a private headless scroll";

// The tabs need a Widget (their colours and hover come from it), which on
// this build needs a compositor: without WAYLAND_DISPLAY the widget tests
// skip and say so. The kit's LayoutProvider and the fonts come from
// ViewsProcessSetup, as in bar/workspace_strip_unittest.cc.
class WorkspaceStripModelBindingTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!HaveWaylandDisplay()) {
      GTEST_SKIP() << kNoDisplay;
    }
    ViewsProcessSetup();
    ax_platform_.emplace();
    harness_ = std::make_unique<ViewsHarness>();
    harness_->SetUp();
    layout_provider_ = std::make_unique<ShellLayoutProvider>();
    widget_ = harness_->MakeWidget();
    widget_->SetBounds(gfx::Rect(0, 0, 220, 400));
    widget_->Show();
  }
  void TearDown() override {
    if (!harness_) {
      return;
    }
    binding_.reset();
    widget_.reset();
    layout_provider_.reset();
    harness_->TearDown();
    harness_.reset();
    ax_platform_.reset();
  }

  // Binds, hosts the strip in the widget and lays it out.
  WorkspaceStrip* Bind() {
    binding_ = std::make_unique<WorkspaceStripModelBinding>(&model_);
    WorkspaceStrip* strip =
        widget_->SetContentsView(binding_->MakeStrip());
    widget_->LayoutRootViewIfNecessary();
    return strip;
  }

  static void Click(Tab* tab) {
    const gfx::Point centre = tab->GetLocalBounds().CenterPoint();
    ui::MouseEvent press(ui::EventType::kMousePressed, centre, centre,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    tab->OnMousePressed(press);
    ui::MouseEvent release(ui::EventType::kMouseReleased, centre, centre,
                           ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
    tab->OnMouseReleased(release);
  }

  std::optional<ui::AXPlatformForTest> ax_platform_;
  std::unique_ptr<ViewsHarness> harness_;
  std::unique_ptr<ShellLayoutProvider> layout_provider_;
  std::unique_ptr<views::Widget> widget_;
  FakeAdapter adapter_;
  WmModel model_{&adapter_};
  std::unique_ptr<WorkspaceStripModelBinding> binding_;
};

TEST(WorkspaceStripModelBindingPureTest, WorkspacesFromSnapshot) {
  const std::vector<Workspace> workspaces =
      WorkspaceStripModelBinding::WorkspacesFromSnapshot(
          MakeSnapshot({{"4", "1"}, {"7", "", true}, {"9", "mail"}}));
  ASSERT_EQ(workspaces.size(), 3u);
  EXPECT_EQ(workspaces[0].id, "4");
  EXPECT_EQ(workspaces[0].title, u"1");
  EXPECT_FALSE(workspaces[0].active);
  // An unnamed workspace shows its id.
  EXPECT_EQ(workspaces[1].title, u"7");
  EXPECT_TRUE(workspaces[1].active);
  EXPECT_EQ(workspaces[2].title, u"mail");
}

TEST_F(WorkspaceStripModelBindingTest, EmptyUntilTheFirstSnapshot) {
  WorkspaceStrip* strip = Bind();
  EXPECT_EQ(strip->GetTabCount(), 0);
  EXPECT_EQ(binding_->apply_count(), 0);

  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}}));
  EXPECT_EQ(binding_->apply_count(), 1);
  EXPECT_EQ(strip->GetTabCount(), 2);
  EXPECT_EQ(Describe(*strip), "1* 2");
}

TEST_F(WorkspaceStripModelBindingTest, ShowsTheSnapshotAModelAlreadyHolds) {
  model_.OnSnapshot(MakeSnapshot({{"4", "1"}, {"7", "web", true}}));
  WorkspaceStrip* strip = Bind();
  EXPECT_EQ(binding_->apply_count(), 1);
  EXPECT_EQ(Describe(*strip), "1 web*");
}

// Rule R6: a click sends FocusWorkspace and changes nothing; the strip moves
// its active tab when the echo arrives as a snapshot.
TEST_F(WorkspaceStripModelBindingTest, ClickIsARequestAndTheEchoRedraws) {
  WorkspaceStrip* strip = Bind();
  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}, {"8", "3"}}));
  widget_->LayoutRootViewIfNecessary();
  const int applied = binding_->apply_count();

  Tab* third = strip->GetTabAt(2);
  ASSERT_TRUE(third);
  Click(third);
  ASSERT_EQ(adapter_.sent.size(), 1u);
  EXPECT_EQ(adapter_.sent[0].kind, WmCommand::Kind::kFocusWorkspace);
  EXPECT_EQ(adapter_.sent[0].workspace, "8");
  EXPECT_EQ(Describe(*strip), "1* 2 3");
  EXPECT_EQ(binding_->apply_count(), applied);
  EXPECT_FALSE(strip->IsActiveTab(third));

  // The echo.
  model_.OnSnapshot(MakeSnapshot({{"4", "1"}, {"7", "2"}, {"8", "3", true}}));
  EXPECT_EQ(Describe(*strip), "1 2 3*");
  EXPECT_EQ(binding_->apply_count(), applied + 1);
  EXPECT_TRUE(strip->IsActiveTab(strip->GetTabAt(2)));
  std::move(adapter_.pending[0]).Run(base::ok());
}

// Without a model (static, niri) the strip moves at once and tells its
// callback; nothing reaches the adapter.
TEST_F(WorkspaceStripModelBindingTest, LocalModeMovesAtOnce) {
  std::vector<Workspace> selected;
  WorkspaceStrip* strip = widget_->SetContentsView(
      std::make_unique<WorkspaceStrip>(
          base::BindLambdaForTesting([&selected](const Workspace& workspace) {
            selected.push_back(workspace);
          }),
          WorkspaceStrip::SelectionMode::kLocal));
  strip->SetWorkspaces({{.id = "a", .title = u"1", .active = true},
                        {.id = "b", .title = u"2"}});
  widget_->LayoutRootViewIfNecessary();

  Click(strip->GetTabAt(1));
  EXPECT_EQ(Describe(*strip), "1 2*");
  ASSERT_EQ(selected.size(), 1u);
  EXPECT_EQ(selected[0].id, "b");
  EXPECT_TRUE(adapter_.sent.empty());
}

}  // namespace
}  // namespace views_shell
