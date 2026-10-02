// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/tabs/workspace_strip_model_binding.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
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

// The tabs need a Widget (hover, focus, the colour provider), the kit's
// LayoutProvider and fonts from the ResourceBundle, as in
// bar/workspace_strip_unittest.cc.
class WorkspaceStripModelBindingTest : public views::ViewsTestBase {
 protected:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    layout_provider_ = std::make_unique<ShellLayoutProvider>();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->SetBounds(gfx::Rect(0, 0, 220, 400));
    widget_->Show();
  }
  void TearDown() override {
    widget_.reset();
    layout_provider_.reset();
    views::ViewsTestBase::TearDown();
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

  ui::AXPlatformForTest ax_platform_;
  std::unique_ptr<ShellLayoutProvider> layout_provider_;
  std::unique_ptr<views::Widget> widget_;
  FakeAdapter adapter_;
  WmModel model_{&adapter_};
  std::unique_ptr<WorkspaceStripModelBinding> binding_;
};

TEST_F(WorkspaceStripModelBindingTest, WorkspacesFromSnapshot) {
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
