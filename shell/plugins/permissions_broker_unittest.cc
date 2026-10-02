// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/permissions_broker.h"

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {
namespace {

PluginManifest Manifest(std::string_view permissions_json) {
  std::optional<base::Value> value = base::JSONReader::Read(
      base::StrCat({R"({"schemaVersion": 1, "id": "test.broker", "name": "b",
        "version": "0.1.0", "engines": {"views-shell": ">=0.1.0", "protocol": 1},
        "runtime": {"mode": "process", "exec": "x"}, "permissions": )",
                    permissions_json, "}"}),
      base::JSON_PARSE_RFC);
  CHECK(value && value->is_dict());
  CHECK(CheckManifestSchema(*value).empty());
  return BuildPluginManifest(std::move(value->GetDict()),
                             base::FilePath("/nonexistent"));
}

class RecordingClient : public PermissionsBroker::Client {
 public:
  void OnNotify(const PluginManifest& plugin,
                const PluginNotification& notification) override {
    notifications.push_back(notification);
  }
  void OnToast(const PluginManifest& plugin,
               const PluginToast& toast) override {
    toasts.push_back(toast);
  }
  std::vector<PluginNotification> notifications;
  std::vector<PluginToast> toasts;
};

class FakeWm : public WmCommandSink {
 public:
  void Send(const WmCommand& command,
            CompositorAdapter::CommandDone done) override {
    commands.push_back(command);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(done),
                       fail ? base::expected<void, WmCommandError>(
                                  base::unexpected(WmCommandError::kRejected))
                            : base::expected<void, WmCommandError>()));
  }
  std::vector<WmCommand> commands;
  bool fail = false;
};

class PermissionsBrokerTest : public testing::Test {
 protected:
  JsonRpcResult Ask(const PluginManifest& plugin,
                    const std::string& method,
                    std::string_view params_json) {
    std::optional<base::Value> params =
        base::JSONReader::Read(params_json, base::JSON_PARSE_RFC);
    CHECK(params && params->is_dict());
    base::test::TestFuture<JsonRpcResult> answer;
    broker_.HandleRequest(plugin, method, params->GetDict(),
                          answer.GetCallback());
    return answer.Take();
  }

  base::test::TaskEnvironment env_;
  RecordingClient client_;
  FakeWm wm_;
  PermissionsBroker broker_{&client_, &wm_};
};

TEST_F(PermissionsBrokerTest, UndeclaredRequestsAnswerMinus32001) {
  const PluginManifest bare = Manifest("[]");
  for (const auto& [method, params, permission] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"notify", R"({"summary": "s"})", "notifications"},
           {"exec", R"({"program": "true", "args": []})", "exec:true"},
           {"compositor/command",
            R"({"capability": "scratchpad.toggle", "args": {}})",
            "compositor:scratchpad.toggle"},
       }) {
    JsonRpcResult result = Ask(bare, method, params);
    ASSERT_FALSE(result.has_value()) << method;
    EXPECT_EQ(result.error().code, kJsonRpcPermissionNotDeclared) << method;
    EXPECT_EQ(result.error().message, "permission not declared");
    EXPECT_EQ(*result.error().data.GetDict().FindString("permission"),
              permission);
  }
  EXPECT_TRUE(client_.notifications.empty());
  EXPECT_TRUE(wm_.commands.empty());

  // A toast needs no permission; an unknown method is -32601.
  EXPECT_TRUE(Ask(bare, "toast", R"({"text": "hi"})").has_value());
  ASSERT_EQ(client_.toasts.size(), 1u);
  EXPECT_EQ(client_.toasts[0].text, "hi");
  JsonRpcResult unknown = Ask(bare, "unlock", "{}");
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(unknown.error().code, kJsonRpcMethodNotFound);
}

TEST_F(PermissionsBrokerTest, ExecRunsTheDeclaredProgram) {
  const PluginManifest plugin = Manifest(R"(["exec:sh"])");
  JsonRpcResult result =
      Ask(plugin, "exec",
          R"({"program": "sh", "args": ["-c", "echo out; echo err >&2; exit 3"]})");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const base::DictValue& outcome = result->GetDict();
  EXPECT_EQ(outcome.FindInt("exitCode"), 3);
  EXPECT_EQ(*outcome.FindString("stdout"), "out\n");
  EXPECT_EQ(*outcome.FindString("stderr"), "err\n");

  // Bad shapes are -32602 before any permission question.
  JsonRpcResult bad = Ask(plugin, "exec", R"({"program": "sh", "args": [1]})");
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().code, kJsonRpcInvalidParams);

  // A program that does not exist answers 127, like a shell.
  const PluginManifest missing = Manifest(R"(["exec:views-shell-no-such"])");
  JsonRpcResult none =
      Ask(missing, "exec", R"({"program": "views-shell-no-such", "args": []})");
  ASSERT_TRUE(none.has_value());
  EXPECT_EQ(none->GetDict().FindInt("exitCode"), 127);
}

TEST_F(PermissionsBrokerTest, CompositorCommandsBecomeTypedWmCommands) {
  const PluginManifest plugin = Manifest(
      R"(["compositor:workspaces.focus", "compositor:scroll.lua"])");
  EXPECT_TRUE(Ask(plugin, "compositor/command",
                  R"({"capability": "workspaces.focus",
                      "args": {"name": "web"}})")
                  .has_value());
  ASSERT_EQ(wm_.commands.size(), 1u);
  EXPECT_EQ(wm_.commands[0].kind, WmCommand::Kind::kFocusWorkspace);
  EXPECT_EQ(wm_.commands[0].name, "web");

  // A node id may come as a number.
  EXPECT_TRUE(Ask(plugin, "compositor/command",
                  R"({"capability": "workspaces.focus",
                      "args": {"workspace": 12}})")
                  .has_value());
  EXPECT_EQ(wm_.commands[1].workspace, "12");

  // Missing arguments and capabilities without a typed verb are -32602.
  JsonRpcResult empty = Ask(plugin, "compositor/command",
                            R"({"capability": "workspaces.focus"})");
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error().code, kJsonRpcInvalidParams);
  JsonRpcResult lua = Ask(plugin, "compositor/command",
                          R"({"capability": "scroll.lua", "args": {}})");
  ASSERT_FALSE(lua.has_value());
  EXPECT_EQ(lua.error().code, kJsonRpcInvalidParams);
  EXPECT_EQ(wm_.commands.size(), 2u);

  // The compositor's refusal comes back as -32000 with its name.
  wm_.fail = true;
  JsonRpcResult refused = Ask(plugin, "compositor/command",
                              R"({"capability": "workspaces.focus",
                                  "args": {"name": "x"}})");
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error().code, kJsonRpcRequestFailed);
  EXPECT_EQ(*refused.error().data.GetDict().FindString("error"), "rejected");
}

TEST_F(PermissionsBrokerTest, NotifyReachesTheClient) {
  const PluginManifest plugin = Manifest(R"(["notifications"])");
  EXPECT_TRUE(Ask(plugin, "notify",
                  R"({"summary": "Echo", "body": "hi", "urgency": "low"})")
                  .has_value());
  ASSERT_EQ(client_.notifications.size(), 1u);
  EXPECT_EQ(client_.notifications[0].summary, "Echo");
  EXPECT_EQ(client_.notifications[0].body, "hi");
  EXPECT_EQ(client_.notifications[0].urgency, "low");
  JsonRpcResult bad =
      Ask(plugin, "notify", R"({"summary": "x", "urgency": "loud"})");
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().code, kJsonRpcInvalidParams);
}

TEST_F(PermissionsBrokerTest, CallAndStateReadFollowTheirPermissions) {
  const PluginManifest plugin = Manifest(
      R"(["call:other.plugin/run", "call:third.plugin/*",
          "state:read:other.plugin/state"])");
  EXPECT_TRUE(PermissionsBroker::AllowsCall(plugin, "other.plugin/run"));
  EXPECT_FALSE(PermissionsBroker::AllowsCall(plugin, "other.plugin/stop"));
  EXPECT_TRUE(PermissionsBroker::AllowsCall(plugin, "third.plugin/anything"));
  EXPECT_TRUE(PermissionsBroker::AllowsCall(plugin, "test.broker/own"));
  EXPECT_TRUE(PermissionsBroker::AllowsStateRead(plugin, "other.plugin/state"));
  EXPECT_FALSE(PermissionsBroker::AllowsStateRead(plugin, "other.plugin/x"));
  EXPECT_TRUE(PermissionsBroker::AllowsStateRead(plugin, "test.broker/mine"));
}

}  // namespace
}  // namespace views_shell
