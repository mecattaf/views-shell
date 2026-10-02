// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_host.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/plugin_registry.h"

namespace views_shell {
namespace {

base::FilePath Data(std::string_view relative) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data").AppendASCII(relative);
}

// What the renderer, the notification service and the surface host would see.
class RecordingDelegate : public PluginHost::Delegate {
 public:
  struct Drawn {
    base::DictValue tree;
    base::DictValue snapshot;
  };
  void OnTree(const std::string& plugin,
              const std::string& surface,
              const base::DictValue& tree,
              const base::DictValue& snapshot) override {
    drawn.insert_or_assign(base::StrCat({plugin, "/", surface}),
                           Drawn{tree.Clone(), snapshot.Clone()});
  }
  void OnSnapshot(const std::string& plugin,
                  const base::DictValue& snapshot) override {
    snapshots[plugin] = snapshot.Clone();
  }
  void OnNotify(const std::string& plugin,
                const PluginNotification& notification) override {
    notified.push_back(plugin);
  }
  void OnToast(const std::string& plugin, const PluginToast& toast) override {}
  void OnCrash(const std::string& plugin,
               const ProcessPlugin::Crash& crash) override {
    crashed.push_back(plugin);
  }
  std::map<std::string, Drawn> drawn;
  std::map<std::string, base::DictValue> snapshots;
  std::vector<std::string> notified;
  std::vector<std::string> crashed;
};

TEST(PluginHostTest, StartsRendersRoutesAndShutsDown) {
  base::test::TaskEnvironment env{
      base::test::TaskEnvironment::MainThreadType::IO};
  PluginRegistry registry{CapabilitySet{"scratchpad.toggle"}};
  registry.Discover({Data("examples/echo-process"),
                     Data("examples/music-scratchpad"),
                     Data("examples/quick-settings-dnd")});
  ASSERT_EQ(registry.plugins().size(), 3u);
  RecordingDelegate delegate;
  PluginHost::Options options;
  options.process.launch_path = ProcessPlugin::LaunchPath::kPlain;
  PluginHost host(&registry, &delegate, /*wm=*/nullptr, std::move(options));
  host.Start();

  // T1 trees arrive at Start with an empty snapshot; T2 trees wait for the
  // first snapshot and arrive with it.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return delegate.drawn.contains("example.echo/button") &&
           delegate.drawn.contains("example.echo/panel");
  }));
  EXPECT_TRUE(delegate.drawn.contains("example.music-scratchpad/button"));
  EXPECT_TRUE(delegate.drawn.contains("views-shell.qs-dnd/tile"));
  EXPECT_TRUE(delegate.drawn.at("views-shell.qs-dnd/tile").snapshot.empty());
  EXPECT_EQ(delegate.drawn.at("example.echo/button").snapshot.FindInt("counter"),
            0);

  // A command from the CLI face, by qualified id.
  base::test::TestFuture<JsonRpcResult> ping;
  host.Invoke("example.echo/ping", base::DictValue().Set("text", "hi"), "cli",
              ping.GetCallback());
  JsonRpcResult answer = ping.Take();
  ASSERT_TRUE(answer.has_value()) << answer.error().message;
  ASSERT_TRUE(
      base::test::RunUntil([&] { return delegate.notified.size() == 1; }));
  EXPECT_EQ(delegate.notified[0], "example.echo");

  // A compositor handler without an adapter is an error, not a hang.
  base::test::TestFuture<JsonRpcResult> toggle;
  host.Invoke("example.music-scratchpad/toggle", base::DictValue(),
              "keybinding", toggle.GetCallback());
  JsonRpcResult toggled = toggle.Take();
  ASSERT_FALSE(toggled.has_value());
  EXPECT_EQ(toggled.error().code, kJsonRpcRequestFailed);

  base::test::TestFuture<JsonRpcResult> unknown;
  host.Invoke("example.echo/nope", base::DictValue(), "cli",
              unknown.GetCallback());
  EXPECT_FALSE(unknown.Take().has_value());

  // A workspace event reaches the plugin that declared it.
  host.DeliverEvent("workspace", base::Value(base::DictValue()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::string* name =
        delegate.snapshots["example.echo"].FindStringByDottedPath(
            "lastEvent.name");
    return name && *name == "workspace";
  }));

  base::test::TestFuture<void> done;
  host.Shutdown(done.GetCallback());
  EXPECT_TRUE(done.Wait());
  EXPECT_EQ(host.process_plugin("example.echo")->state(),
            ProcessPlugin::State::kExited);
  EXPECT_TRUE(delegate.crashed.empty());
}

}  // namespace
}  // namespace views_shell
