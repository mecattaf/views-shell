// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// WmModel's snapshot diff on hand-written snapshots, and its request path
// (the model never changes before the echo).

#include "views_shell/wm/wm_model.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

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

// Applies the callbacks to its own tree, as a rail would, and logs them.
class Mirror : public WmModel::Observer {
 public:
  void OnChildAdded(const std::string& parent,
                    const std::string& id,
                    int index) override {
    std::vector<std::string>& list = tree[parent];
    CHECK_LE(static_cast<size_t>(index), list.size());
    CHECK(!base::Contains(list, id));
    list.insert(list.begin() + index, id);
    log.push_back(base::StrCat(
        {"add ", parent, "/", id, "@", base::NumberToString(index)}));
  }
  void OnChildRemoved(const std::string& parent,
                      const std::string& id) override {
    CHECK(base::Contains(tree[parent], id));
    std::erase(tree[parent], id);
    log.push_back(base::StrCat({"remove ", parent, "/", id}));
  }
  void OnChildMoved(const std::string& old_parent,
                    const std::string& new_parent,
                    const std::string& id,
                    int new_index) override {
    CHECK(base::Contains(tree[old_parent], id));
    std::erase(tree[old_parent], id);
    std::vector<std::string>& list = tree[new_parent];
    CHECK_LE(static_cast<size_t>(new_index), list.size());
    list.insert(list.begin() + new_index, id);
    log.push_back(base::StrCat({"move ", old_parent, ">", new_parent, "/", id,
                                "@", base::NumberToString(new_index)}));
  }
  void OnItemChanged(const std::string& id) override {
    log.push_back("changed " + id);
  }
  void OnOutputsChanged() override { log.push_back("outputs"); }
  void OnMruChanged() override { log.push_back("mru"); }
  void OnFocusChanged(const std::string& workspace,
                      const std::string& window) override {
    log.push_back(base::StrCat({"focus ", workspace, "/", window}));
  }
  void OnSnapshotApplied() override { ++applied; }

  // The tree without empty lists.
  std::map<std::string, std::vector<std::string>> Tree() const {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& [parent, list] : tree) {
      if (!list.empty()) {
        out[parent] = list;
      }
    }
    return out;
  }

  std::map<std::string, std::vector<std::string>> tree;
  std::vector<std::string> log;
  int applied = 0;
};

// A snapshot from a compact description: workspaces "id:name", the first
// one focused, and windows "id@workspace" ("id@" for the scratchpad).
WmSnapshot Make(std::vector<std::string> workspaces,
                std::vector<std::string> windows,
                std::string focused_window = std::string()) {
  WmSnapshot snapshot;
  WmOutput output;
  output.id = "HEADLESS-1";
  snapshot.outputs.push_back(output);
  int index = 0;
  for (const std::string& spec : workspaces) {
    WmWorkspace workspace;
    const size_t colon = spec.find(':');
    workspace.id = spec.substr(0, colon);
    workspace.name = spec.substr(colon + 1);
    workspace.output = output.id;
    workspace.index = index;
    workspace.focused = workspace.active = index == 0;
    snapshot.workspaces.push_back(workspace);
    ++index;
  }
  for (const std::string& spec : windows) {
    WmWindow window;
    const size_t at = spec.find('@');
    window.id = spec.substr(0, at);
    window.workspace = spec.substr(at + 1);
    window.title = "w" + window.id;
    window.focused = window.id == focused_window;
    snapshot.windows.push_back(window);
    snapshot.mru.push_back(window.id);
  }
  return snapshot;
}

// What the mirror must hold after applying a snapshot.
std::map<std::string, std::vector<std::string>> Expected(
    const WmSnapshot& snapshot) {
  std::map<std::string, std::vector<std::string>> out;
  for (const WmWorkspace& workspace : snapshot.workspaces) {
    out[std::string()].push_back(workspace.id);
  }
  for (const WmWindow& window : snapshot.windows) {
    out[window.workspace.empty() ? std::string(WmModel::kScratchpadParent)
                                 : window.workspace]
        .push_back(window.id);
  }
  return out;
}

class WmModelTest : public ::testing::Test {
 protected:
  WmModelTest() : model_(&adapter_) { model_.AddObserver(&mirror_); }
  ~WmModelTest() override { model_.RemoveObserver(&mirror_); }

  base::test::TaskEnvironment task_environment_;
  FakeAdapter adapter_;
  WmModel model_;
  Mirror mirror_;
};

TEST_F(WmModelTest, FirstSnapshotAddsEverythingInOrder) {
  model_.OnSnapshot(Make({"4:1", "7:web"}, {"5@4", "6@4", "8@7"}, "5"));
  EXPECT_EQ((std::vector<std::string>{"add /4@0", "add /7@1", "add 4/5@0",
                                      "add 4/6@1", "add 7/8@0", "outputs",
                                      "mru", "focus 4/5"}),
            mirror_.log);
  EXPECT_EQ(1, mirror_.applied);
  EXPECT_TRUE(model_.has_snapshot());
}

TEST_F(WmModelTest, DiffsRemovalsMovesAndAdditions) {
  model_.OnSnapshot(
      Make({"4:1", "7:web", "9:mail"}, {"5@4", "6@4", "8@7", "10@9"}, "5"));
  mirror_.log.clear();
  // Workspace 9 goes away with window 10 moving to 4; window 6 closes; a new
  // workspace 11 arrives first; window 8 goes to the scratchpad; 12 opens.
  const WmSnapshot next =
      Make({"11:new", "4:1", "7:web"}, {"10@4", "5@4", "12@7", "8@"}, "5");
  model_.OnSnapshot(next);
  EXPECT_EQ(
      (std::vector<std::string>{"remove 4/6", "add /11@0", "move 9>4/10@1",
                                "move 7>@scratchpad/8@0", "move 4>4/10@0",
                                "add 7/12@0", "remove /9",
                                "changed 4",  // index and focus moved
                                "changed 7",  // index
                                "mru", "focus 11/5"}),
      mirror_.log);
  EXPECT_EQ(Expected(next), mirror_.Tree());
}

TEST_F(WmModelTest, MirrorFollowsEverySequenceOfSnapshots) {
  const std::vector<WmSnapshot> steps = {
      Make({"1:a"}, {"10@1"}),
      Make({"2:b", "1:a"}, {"11@2", "10@1"}),
      Make({"1:a", "2:b", "3:c"}, {"10@2", "11@2", "12@3", "13@1"}),
      Make({"3:c"}, {"13@3", "12@3", "10@", "11@"}),
      Make({"4:d", "3:c"}, {"11@4", "10@4", "12@3"}),
      Make({}, {"10@", "12@"}),
      Make({"5:e"}, {}),
  };
  for (size_t i = 0; i < steps.size(); ++i) {
    model_.OnSnapshot(steps[i]);
    EXPECT_EQ(Expected(steps[i]), mirror_.Tree()) << "step " << i;
    EXPECT_EQ(steps[i], model_.snapshot()) << "step " << i;
  }
  EXPECT_EQ(static_cast<int>(steps.size()), mirror_.applied);
}

TEST_F(WmModelTest, ReportsItemFocusOutputAndMruChanges) {
  model_.OnSnapshot(Make({"4:1", "7:web"}, {"5@4", "8@7"}, "5"));
  mirror_.log.clear();

  WmSnapshot renamed = Make({"4:1", "7:mail"}, {"5@4", "8@7"}, "5");
  model_.OnSnapshot(renamed);
  EXPECT_EQ(std::vector<std::string>{"changed 7"}, mirror_.log);
  mirror_.log.clear();

  WmSnapshot titled = renamed;
  titled.windows[1].title = "inbox";
  titled.windows[1].urgent = true;
  titled.outputs[0].scale = 2.0;
  model_.OnSnapshot(titled);
  EXPECT_EQ((std::vector<std::string>{"changed 8", "outputs"}), mirror_.log);
  mirror_.log.clear();

  WmSnapshot focused = titled;
  focused.windows[0].focused = false;
  focused.windows[1].focused = true;
  focused.mru = {"8", "5"};
  model_.OnSnapshot(focused);
  EXPECT_EQ(
      (std::vector<std::string>{"changed 5", "changed 8", "mru", "focus 4/8"}),
      mirror_.log);
  mirror_.log.clear();

  // The same snapshot again: nothing but the applied tick.
  const int applied = mirror_.applied;
  model_.OnSnapshot(focused);
  EXPECT_TRUE(mirror_.log.empty());
  EXPECT_EQ(applied + 1, mirror_.applied);
}

TEST_F(WmModelTest, RequestsChangeNothingUntilTheEcho) {
  const WmSnapshot before = Make({"4:1"}, {"5@4"}, "5");
  model_.OnSnapshot(before);
  mirror_.log.clear();

  bool done = false;
  model_.FocusWorkspaceByName(
      "3", base::BindOnce(
               [](bool* done, base::expected<void, WmCommandError> result) {
                 *done = result.has_value();
               },
               &done));
  model_.MoveWindow("5", "4");
  model_.RenameWorkspace("4", "one");
  ASSERT_EQ(3u, adapter_.sent.size());
  EXPECT_EQ(WmCommand::Kind::kFocusWorkspace, adapter_.sent[0].kind);
  EXPECT_EQ("3", adapter_.sent[0].name);
  EXPECT_TRUE(adapter_.sent[0].workspace.empty());
  EXPECT_EQ(WmCommand::Kind::kMoveWindowToWorkspace, adapter_.sent[1].kind);
  EXPECT_EQ("5", adapter_.sent[1].window);
  EXPECT_EQ("4", adapter_.sent[1].workspace);
  EXPECT_EQ(WmCommand::Kind::kRenameWorkspace, adapter_.sent[2].kind);
  // Sent, not applied.
  EXPECT_EQ(before, model_.snapshot());
  EXPECT_TRUE(mirror_.log.empty());

  // The echo: the compositor's next snapshot.
  model_.OnSnapshot(Make({"9:3", "4:1"}, {"5@4"}, "5"));
  std::move(adapter_.pending[0]).Run(base::ok());
  EXPECT_TRUE(done);
  ASSERT_TRUE(model_.snapshot().FocusedWorkspace());
  EXPECT_EQ("3", model_.snapshot().FocusedWorkspace()->name);
  EXPECT_EQ("focus 9/5", mirror_.log.back());
}

}  // namespace
}  // namespace views_shell
