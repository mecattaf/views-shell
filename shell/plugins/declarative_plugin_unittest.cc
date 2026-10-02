// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/declarative_plugin.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/permissions_broker.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {
namespace {

base::FilePath Data(std::string_view relative) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data").AppendASCII(relative);
}

class RecordingSink : public TreeSink {
 public:
  void OnTree(const PluginManifest& plugin,
              const std::string& surface,
              base::DictValue tree) override {
    trees.insert_or_assign(surface, std::move(tree));
  }
  void OnSnapshot(const PluginManifest& plugin, base::DictValue data) override {
  }
  void OnSourceSnapshot(const PluginManifest& plugin,
                        const std::string& source,
                        base::Value data) override {}
  std::map<std::string, base::DictValue> trees;
};

class NullClient : public PermissionsBroker::Client {
 public:
  void OnNotify(const PluginManifest&, const PluginNotification&) override {}
  void OnToast(const PluginManifest&, const PluginToast&) override {}
};

class FakeWm : public WmCommandSink {
 public:
  void Send(const WmCommand& command,
            CompositorAdapter::CommandDone done) override {
    commands.push_back(command);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done),
                                  base::expected<void, WmCommandError>()));
  }
  std::vector<WmCommand> commands;
};

class RecordingHost : public DeclarativePlugin::Host {
 public:
  void OpenControlPage(const PluginManifest& plugin,
                       const std::string& page) override {
    opened.push_back(page);
  }
  void CallCommand(const PluginManifest& caller,
                   const std::string& command,
                   base::DictValue args,
                   DeclarativePlugin::ResultCallback callback) override {
    called.push_back(command);
    std::move(callback).Run(base::Value(base::DictValue()));
  }
  std::vector<std::string> opened;
  std::vector<std::string> called;
};

class DeclarativePluginTest : public testing::Test {
 protected:
  JsonRpcResult Invoke(DeclarativePlugin& plugin, std::string_view command) {
    base::test::TestFuture<JsonRpcResult> answer;
    plugin.Invoke(command, base::DictValue(), answer.GetCallback());
    return answer.Take();
  }

  base::test::TaskEnvironment env_;
  RecordingSink sink_;
  NullClient client_;
  FakeWm wm_;
  PermissionsBroker broker_{&client_, &wm_};
  RecordingHost host_;
};

TEST_F(DeclarativePluginTest, MusicScratchpadDrawsItsButtonAndTogglesTheScratchpad) {
  base::expected<PluginManifest, std::string> manifest =
      LoadPluginManifest(Data("examples/music-scratchpad"));
  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  DeclarativePlugin plugin(*manifest, &sink_, &broker_, &host_);
  EXPECT_TRUE(plugin.Load().empty());
  ASSERT_EQ(sink_.trees.size(), 1u);
  const base::DictValue& tree = sink_.trees.at("button");
  EXPECT_EQ(*tree.FindStringByDottedPath("root.type"), "iconButton");
  EXPECT_EQ(*tree.FindStringByDottedPath("root.action.command"), "toggle");

  // The tree's action and the F9 key both name toggle; its handler is the
  // typed scratchpad.toggle, allowed by compositor:scratchpad.toggle.
  JsonRpcResult result = Invoke(plugin, "toggle");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(*result, base::Value(base::DictValue()));
  ASSERT_EQ(wm_.commands.size(), 1u);
  EXPECT_EQ(wm_.commands[0].kind, WmCommand::Kind::kToggleScratchpad);
  EXPECT_TRUE(Invoke(plugin, "example.music-scratchpad/toggle").has_value());
  EXPECT_EQ(wm_.commands.size(), 2u);

  JsonRpcResult unknown = Invoke(plugin, "nope");
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(unknown.error().code, kJsonRpcInvalidParams);
}

TEST_F(DeclarativePluginTest, QuickSettingsDndTileLoadsAndBindsToItsSource) {
  base::expected<PluginManifest, std::string> manifest =
      LoadPluginManifest(Data("examples/quick-settings-dnd"));
  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  DeclarativePlugin plugin(*manifest, &sink_, &broker_, &host_);
  EXPECT_TRUE(plugin.Load().empty());
  EXPECT_EQ(plugin.loaded(), (std::vector<std::string>{"tile"}));
  const base::DictValue& tree = sink_.trees.at("tile");
  EXPECT_EQ(*tree.FindStringByDottedPath("root.type"), "tile");
  // The action names another plugin's command, which call: covers.
  EXPECT_EQ(*tree.FindStringByDottedPath("root.action.command"),
            "views-shell.notifications/toggle-quiet-mode");
  EXPECT_TRUE(PermissionsBroker::AllowsCall(
      *manifest, "views-shell.notifications/toggle-quiet-mode"));
  ASSERT_EQ(manifest->quick_settings.size(), 1u);
  EXPECT_EQ(manifest->quick_settings[0].state,
            "views-shell.notifications/state");
}

TEST_F(DeclarativePluginTest, OpenAndCallHandlersGoThroughTheHost) {
  std::optional<base::Value> value = base::JSONReader::Read(R"({
    "schemaVersion": 1, "id": "test.handlers", "name": "h", "version": "0.1.0",
    "engines": {"views-shell": ">=0.1.0"}, "runtime": {"mode": "declarative"},
    "permissions": ["call:other.plugin/run"],
    "contributes": {"commands": [
      {"id": "settings", "title": "S", "handler": {"open": "settings/plugins"}},
      {"id": "forward", "title": "F", "handler": {"call": "other.plugin/run"}},
      {"id": "local", "title": "L", "handler": {"call": "settings"}},
      {"id": "sneak", "title": "X", "handler": {"call": "third.plugin/run"}}
    ]}})",
                                                            base::JSON_PARSE_RFC);
  ASSERT_TRUE(value && value->is_dict());
  ASSERT_TRUE(CheckManifestSchema(*value).empty());
  const PluginManifest manifest = BuildPluginManifest(
      std::move(value->GetDict()), base::FilePath("/nonexistent"));
  DeclarativePlugin plugin(manifest, &sink_, &broker_, &host_);

  EXPECT_TRUE(Invoke(plugin, "settings").has_value());
  EXPECT_EQ(host_.opened, (std::vector<std::string>{"settings/plugins"}));
  EXPECT_TRUE(Invoke(plugin, "forward").has_value());
  EXPECT_TRUE(Invoke(plugin, "local").has_value());
  EXPECT_EQ(host_.called, (std::vector<std::string>{
                              "other.plugin/run", "test.handlers/settings"}));
  // validate.py rejects this manifest at install; the handler still refuses.
  JsonRpcResult sneak = Invoke(plugin, "sneak");
  ASSERT_FALSE(sneak.has_value());
  EXPECT_EQ(sneak.error().code, kJsonRpcPermissionNotDeclared);
  EXPECT_EQ(host_.called.size(), 2u);
}

}  // namespace
}  // namespace views_shell
