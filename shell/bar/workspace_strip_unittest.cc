// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The bar's workspace strip against a WmModel fed by a fake adapter: the
// strip follows the model's callbacks only, and a click is a request whose
// echo moves the focus (rule R6). No display: the views are never in a widget.

#include "views_shell/bar/workspace_strip.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/task_environment.h"
#include "base/time/default_clock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "views_shell/bar/bar_view.h"
#include "views_shell/bar/clock_view.h"
#include "views_shell/style/layout_provider.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/wm/wm_model.h"

namespace views_shell::bar {
namespace {

// Records commands; never answers them on its own.
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
  bool urgent = false;
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
    workspace.urgent = spec.urgent;
    snapshot.workspaces.push_back(std::move(workspace));
  }
  return snapshot;
}

// The names, in order, with '*' after the focused one and '!' after an
// urgent one.
std::string Describe(const WorkspaceStrip& strip) {
  std::string out;
  for (const WorkspaceStrip::ButtonState& state : strip.GetButtonStates()) {
    if (!out.empty()) {
      out += " ";
    }
    out += base::UTF16ToUTF8(state.label);
    if (state.focused) {
      out += "*";
    }
    if (state.urgent) {
      out += "!";
    }
  }
  return out;
}

// Views need the kit's LayoutProvider and fonts from the ResourceBundle, as
// in bar_view_unittest.cc.
class WorkspaceStripTest : public testing::Test {
 protected:
  void SetUp() override {
    static const bool kPathsRegistered = [] {
      ui::RegisterPathProvider();
      return true;
    }();
    ASSERT_TRUE(kPathsRegistered);
    base::FilePath pak;
    ASSERT_TRUE(base::PathService::Get(ui::UI_TEST_PAK, &pak));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);
    layout_provider_ = std::make_unique<ShellLayoutProvider>();
  }
  void TearDown() override {
    layout_provider_.reset();
    ui::ResourceBundle::CleanupSharedInstance();
  }

  ui::AXPlatformForTest ax_platform_;
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<ShellLayoutProvider> layout_provider_;
  FakeAdapter adapter_;
  WmModel model_{&adapter_};
};

TEST_F(WorkspaceStripTest, EmptyUntilTheFirstSnapshot) {
  WorkspaceStrip strip(&model_);
  EXPECT_TRUE(strip.children().empty());
  EXPECT_EQ(strip.rebuild_count(), 0);

  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}}));
  EXPECT_EQ(strip.rebuild_count(), 1);
  EXPECT_EQ(Describe(strip), "1* 2");
}

TEST_F(WorkspaceStripTest, ShowsTheSnapshotAModelAlreadyHolds) {
  model_.OnSnapshot(MakeSnapshot({{"4", "1"}, {"7", "web", true}}));
  WorkspaceStrip strip(&model_);
  EXPECT_EQ(Describe(strip), "1 web*");
  EXPECT_EQ(strip.children().size(), 2u);
}

TEST_F(WorkspaceStripTest, OneButtonPerWorkspaceWithRoles) {
  WorkspaceStrip strip(&model_);
  // An unnamed workspace shows its id.
  model_.OnSnapshot(MakeSnapshot(
      {{"4", "1", true}, {"7", ""}, {"9", "mail", false, /*urgent=*/true}}));
  EXPECT_EQ(Describe(strip), "1* 7 mail!");

  views::LabelButton* focused = strip.GetButtonForWorkspace("4");
  views::LabelButton* plain = strip.GetButtonForWorkspace("7");
  views::LabelButton* urgent = strip.GetButtonForWorkspace("9");
  ASSERT_TRUE(focused && plain && urgent);
  ASSERT_TRUE(focused->background());
  EXPECT_EQ(focused->background()->color(),
            WorkspaceStrip::kFocusedBackgroundId);
  EXPECT_EQ(focused->background()->color(), ui::kColorSysPrimary);
  EXPECT_FALSE(plain->background());
  ASSERT_TRUE(urgent->background());
  EXPECT_EQ(urgent->background()->color(), ui::kColorSysError);
  EXPECT_EQ(focused->GetAccessibleName(), u"1");
}

// Rule R6: a click sends FocusWorkspace and changes nothing; the strip
// moves the focus when the echo arrives as a snapshot.
TEST_F(WorkspaceStripTest, ClickIsARequestAndTheEchoRedraws) {
  WorkspaceStrip strip(&model_);
  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}, {"8", "3"}}));
  const int rebuilds = strip.rebuild_count();

  views::test::ButtonTestApi(strip.GetButtonForWorkspace("8"))
      .NotifyDefaultMouseClick();
  ASSERT_EQ(adapter_.sent.size(), 1u);
  EXPECT_EQ(adapter_.sent[0].kind, WmCommand::Kind::kFocusWorkspace);
  EXPECT_EQ(adapter_.sent[0].workspace, "8");
  EXPECT_EQ(Describe(strip), "1* 2 3");
  EXPECT_EQ(strip.rebuild_count(), rebuilds);
  EXPECT_FALSE(strip.GetButtonForWorkspace("8")->background());

  // The echo.
  model_.OnSnapshot(MakeSnapshot({{"4", "1"}, {"7", "2"}, {"8", "3", true}}));
  EXPECT_EQ(Describe(strip), "1 2 3*");
  EXPECT_EQ(strip.rebuild_count(), rebuilds + 1);
  ASSERT_TRUE(strip.GetButtonForWorkspace("8")->background());
  EXPECT_EQ(strip.GetButtonForWorkspace("8")->background()->color(),
            ui::kColorSysPrimary);
  std::move(adapter_.pending[0]).Run(base::ok());
}

// Buttons are kept by workspace id across snapshots; new workspaces get a
// button, gone ones lose theirs, and the order is the snapshot's.
TEST_F(WorkspaceStripTest, FollowsAddsRemovesRenamesAndMoves) {
  WorkspaceStrip strip(&model_);
  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}}));
  views::LabelButton* first = strip.GetButtonForWorkspace("4");

  model_.OnSnapshot(
      MakeSnapshot({{"7", "two"}, {"4", "1", true}, {"11", "3"}}));
  EXPECT_EQ(Describe(strip), "two 1* 3");
  EXPECT_EQ(strip.GetButtonForWorkspace("4"), first);
  EXPECT_EQ(strip.GetIndexOf(first), 1u);

  model_.OnSnapshot(MakeSnapshot({{"11", "3", true}}));
  EXPECT_EQ(Describe(strip), "3*");
  EXPECT_EQ(strip.children().size(), 1u);
  EXPECT_FALSE(strip.GetButtonForWorkspace("4"));
}

// A disconnect keeps the last workspaces on screen until the resync.
TEST_F(WorkspaceStripTest, KeepsTheLastSnapshotOverADisconnect) {
  WorkspaceStrip strip(&model_);
  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}}));
  model_.OnDisconnected();
  EXPECT_EQ(Describe(strip), "1* 2");
}

// In the bar: the strip fills the left section, the clock keeps the right
// edge and the focused window's title is centred between them.
TEST_F(WorkspaceStripTest, SitsInTheBarsLeftSectionWithACentredTitle) {
  model_.OnSnapshot(MakeSnapshot({{"4", "1", true}, {"7", "2"}}));
  BarView bar(base::DefaultClock::GetInstance());
  WorkspaceStrip* strip =
      bar.SetLeftView(std::make_unique<WorkspaceStrip>(&model_));
  bar.SetTitle(u"a window title");
  bar.SetBounds(0, 0, 1920, 32);
  bar.DeprecatedLayoutImmediately();

  EXPECT_EQ(strip->parent(), bar.left_section());
  EXPECT_EQ(strip->bounds(), gfx::Rect(bar.left_section()->size()));
  EXPECT_EQ(strip->children().size(), 2u);
  EXPECT_GT(strip->children()[1]->bounds().x(), 0);
  EXPECT_EQ(bar.clock_view()->bounds().right(),
            1920 - layout_provider_->GetDistanceMetric(
                       views::DISTANCE_RELATED_CONTROL_HORIZONTAL));

  const gfx::Rect title = bar.title_label()->bounds();
  EXPECT_TRUE(bar.title_label()->GetVisible());
  EXPECT_GT(title.width(), 0);
  EXPECT_LE(std::abs(title.CenterPoint().x() - 960), 1);

  bar.SetTitle(u"");
  EXPECT_FALSE(bar.title_label()->GetVisible());
}

}  // namespace
}  // namespace views_shell::bar
