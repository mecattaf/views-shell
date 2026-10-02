// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// T2 sessions against real processes: the Python reference plugin
// (examples/echo-process, run by the python3 on PATH through its #! line) and
// two test-only shell plugins under testdata/.

#include "views_shell/plugins/process_plugin.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/test/run_until.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/plugins/permissions_broker.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {
namespace {

base::FilePath SrcRoot() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root;
}

bool Python3OnPath() {
  const std::string path =
      base::Environment::Create()->GetVar("PATH").value_or("");
  for (std::string_view dir : base::SplitStringPiece(
           path, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (base::PathExists(base::FilePath(dir).Append("python3"))) {
      return true;
    }
  }
  return false;
}

class RecordingSink : public TreeSink {
 public:
  void OnTree(const PluginManifest& plugin,
              const std::string& surface,
              base::DictValue tree) override {
    trees.insert_or_assign(surface, std::move(tree));
  }
  void OnSnapshot(const PluginManifest& plugin, base::DictValue data) override {
    snapshot = std::move(data);
    ++snapshots;
  }
  void OnSourceSnapshot(const PluginManifest& plugin,
                        const std::string& source,
                        base::Value data) override {}

  std::map<std::string, base::DictValue> trees;
  std::optional<base::DictValue> snapshot;
  int snapshots = 0;
};

class RecordingClient : public PermissionsBroker::Client {
 public:
  void OnNotify(const PluginManifest& plugin,
                const PluginNotification& notification) override {
    notifications.push_back(notification);
  }
  void OnToast(const PluginManifest& plugin,
               const PluginToast& toast) override {}
  std::vector<PluginNotification> notifications;
};

class RecordingObserver : public ProcessPlugin::Observer {
 public:
  void OnPluginRunning(const ProcessPlugin& plugin) override { ++running; }
  void OnPluginCrashed(const ProcessPlugin& plugin,
                       const ProcessPlugin::Crash& crash) override {
    crashes.push_back(crash);
  }
  int running = 0;
  std::vector<ProcessPlugin::Crash> crashes;
};

class ProcessPluginTest : public testing::Test {
 protected:
  PluginManifest Load(std::string_view relative) {
    base::expected<PluginManifest, std::string> manifest =
        LoadPluginManifest(SrcRoot().AppendASCII(relative));
    CHECK(manifest.has_value()) << manifest.error();
    return std::move(manifest).value();
  }

  JsonRpcResult Invoke(ProcessPlugin& plugin,
                       std::string_view command,
                       base::DictValue args) {
    base::test::TestFuture<JsonRpcResult> answer;
    plugin.Invoke(command, std::move(args), "cli", std::nullopt,
                  answer.GetCallback());
    return answer.Take();
  }

  int SnapshotInt(std::string_view key) {
    return sink_.snapshot ? sink_.snapshot->FindInt(key).value_or(-1) : -1;
  }

  base::test::TaskEnvironment env_{
      base::test::TaskEnvironment::MainThreadType::IO};
  RecordingSink sink_;
  RecordingClient client_;
  RecordingObserver observer_;
  PermissionsBroker broker_{&client_, nullptr};
};

// The whole protocol against the Python reference plugin.
TEST_F(ProcessPluginTest, EchoReferencePluginSession) {
  ASSERT_TRUE(Python3OnPath())
      << "examples/echo-process runs with the python3 on PATH";
  const PluginManifest manifest =
      Load("views_shell/data/examples/echo-process");
  ProcessPlugin::Options options;
  options.launch_path = ProcessPlugin::LaunchPath::kAuto;
  ProcessPlugin plugin(manifest, std::move(options), &sink_, &broker_,
                       &observer_);
  plugin.Start();

  // initialize, then both trees and a snapshot within the startup window.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return plugin.state() == ProcessPlugin::State::kRunning && sink_.snapshot &&
           sink_.trees.size() == 2;
  }));
  LOG(INFO) << "launch path taken: "
            << ProcessPlugin::LaunchPathName(plugin.launch_path());
  EXPECT_NE(plugin.launch_path(), ProcessPlugin::LaunchPath::kAuto);
  EXPECT_EQ(observer_.running, 1);
  EXPECT_TRUE(sink_.trees.contains("button"));
  EXPECT_TRUE(sink_.trees.contains("panel"));
  EXPECT_EQ(SnapshotInt("counter"), 0);
  EXPECT_EQ(*sink_.snapshot->FindString("greeting"), "hello");

  // ping: a json result, and a notify the manifest's permission allows.
  JsonRpcResult ping =
      Invoke(plugin, "ping", base::DictValue().Set("text", "hi"));
  ASSERT_TRUE(ping.has_value()) << ping.error().message;
  EXPECT_EQ(*ping->GetDict().FindStringByDottedPath("result.args.text"), "hi");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return client_.notifications.size() == 1; }));
  EXPECT_EQ(client_.notifications[0].body, "hi");

  // set-counter: {} and a new whole snapshot.
  JsonRpcResult set =
      Invoke(plugin, "set-counter", base::DictValue().Set("value", 5));
  ASSERT_TRUE(set.has_value());
  ASSERT_TRUE(base::test::RunUntil([&] { return SnapshotInt("counter") == 5; }));
  EXPECT_EQ(SnapshotInt("next"), 6);

  // config/changed carries the full config.
  base::test::TestFuture<JsonRpcResult> config;
  plugin.ConfigChanged(base::DictValue().Set("greeting", "hey"),
                       config.GetCallback());
  EXPECT_TRUE(config.Take().has_value());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return *sink_.snapshot->FindString("greeting") == "hey";
  }));

  // An event the manifest declares arrives; one it does not is never sent.
  EXPECT_TRUE(plugin.DeliverEvent("workspace",
                                  base::Value(base::DictValue().Set("n", 2))));
  EXPECT_FALSE(plugin.DeliverEvent("window", base::Value()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::string* name =
        sink_.snapshot->FindStringByDottedPath("lastEvent.name");
    return name && *name == "workspace";
  }));

  // try-exec: the broker refuses exec:true with -32001 and the plugin lives.
  JsonRpcResult exec = Invoke(plugin, "try-exec", base::DictValue());
  ASSERT_TRUE(exec.has_value());
  EXPECT_EQ(exec->GetDict().FindIntByDottedPath("result.answer.error.code"),
            kJsonRpcPermissionNotDeclared);
  EXPECT_TRUE(plugin.IsAlive());
  base::test::TestFuture<JsonRpcResult> opened;
  plugin.SurfaceOpened("panel", "1", opened.GetCallback());
  JsonRpcResult opened_answer = opened.Take();
  ASSERT_TRUE(opened_answer.has_value());
  EXPECT_EQ(*opened_answer, base::Value(base::DictValue()));

  // Undeclared commands and bad args never reach the plugin.
  JsonRpcResult undeclared = Invoke(plugin, "nope", base::DictValue());
  ASSERT_FALSE(undeclared.has_value());
  EXPECT_EQ(undeclared.error().code, kJsonRpcInvalidParams);
  JsonRpcResult bad_arg =
      Invoke(plugin, "set-counter", base::DictValue().Set("value", "five"));
  ASSERT_FALSE(bad_arg.has_value());
  EXPECT_EQ(bad_arg.error().code, kJsonRpcInvalidParams);

  // shutdown: answered, stdin closed, exit 0 within the 1 s grace period.
  const base::TimeTicks start = base::TimeTicks::Now();
  base::test::TestFuture<int> exit_code;
  plugin.Shutdown(exit_code.GetCallback());
  EXPECT_EQ(exit_code.Get(), 0);
  EXPECT_LT(base::TimeTicks::Now() - start, base::Seconds(1));
  EXPECT_EQ(plugin.state(), ProcessPlugin::State::kExited);
  EXPECT_TRUE(observer_.crashes.empty());
}

// A plugin that exits before initialize is restarted with doubling backoff,
// and given up after max_restarts consecutive crashes.
TEST_F(ProcessPluginTest, CrashRestartsWithBackoffThenFails) {
  const PluginManifest manifest =
      Load("views_shell/plugins/testdata/exits-at-once");
  ProcessPlugin::Options options;
  options.launch_path = ProcessPlugin::LaunchPath::kPlain;
  options.initial_backoff = base::Milliseconds(40);
  options.max_restarts = 2;
  ProcessPlugin plugin(manifest, std::move(options), &sink_, &broker_,
                       &observer_);
  plugin.Start();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return plugin.state() == ProcessPlugin::State::kFailed; }));
  EXPECT_EQ(plugin.launch_path(), ProcessPlugin::LaunchPath::kPlain);
  EXPECT_EQ(plugin.launches(), 3);
  ASSERT_EQ(observer_.crashes.size(), 3u);
  for (size_t i = 0; i < observer_.crashes.size(); ++i) {
    const ProcessPlugin::Crash& crash = observer_.crashes[i];
    EXPECT_EQ(crash.plugin_id, "test.exits-at-once");
    EXPECT_EQ(crash.exit_code, 3);
    EXPECT_EQ(crash.reason, "exited");
    EXPECT_EQ(crash.consecutive, static_cast<int>(i) + 1);
  }
  EXPECT_EQ(observer_.crashes[0].restart_in, base::Milliseconds(40));
  EXPECT_EQ(observer_.crashes[1].restart_in, base::Milliseconds(80));
  EXPECT_FALSE(observer_.crashes[2].restart_in.has_value());
  EXPECT_EQ(observer_.running, 0);

  // A failed plugin answers surface calls with -32000 instead of hanging.
  base::test::TestFuture<JsonRpcResult> closed;
  plugin.SurfaceClosed("panel", "1", closed.GetCallback());
  JsonRpcResult answer = closed.Take();
  ASSERT_FALSE(answer.has_value());
  EXPECT_EQ(answer.error().code, kJsonRpcRequestFailed);
}

// Any protocol but 1 means the host does not start the plugin, and does not
// restart it.
TEST_F(ProcessPluginTest, ProtocolMismatchIsNeverStarted) {
  const PluginManifest manifest =
      Load("views_shell/plugins/testdata/wrong-protocol");
  ProcessPlugin::Options options;
  options.launch_path = ProcessPlugin::LaunchPath::kPlain;
  ProcessPlugin plugin(manifest, std::move(options), &sink_, &broker_,
                       &observer_);
  plugin.Start();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return plugin.state() == ProcessPlugin::State::kFailed; }));
  ASSERT_EQ(observer_.crashes.size(), 1u);
  EXPECT_EQ(observer_.crashes[0].reason, "protocol mismatch");
  EXPECT_FALSE(observer_.crashes[0].restart_in.has_value());
  EXPECT_EQ(plugin.launches(), 1);
  EXPECT_EQ(observer_.running, 0);
}

// The plain path, forced: what every test above takes inside runtime-test.
TEST_F(ProcessPluginTest, PlainLaunchPathRunsTheReferencePlugin) {
  ASSERT_TRUE(Python3OnPath());
  const PluginManifest manifest =
      Load("views_shell/data/examples/echo-process");
  ProcessPlugin::Options options;
  options.launch_path = ProcessPlugin::LaunchPath::kPlain;
  options.config.Set("greeting", "plain");
  ProcessPlugin plugin(manifest, std::move(options), &sink_, &broker_,
                       &observer_);
  plugin.Start();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return plugin.IsAlive() && sink_.snapshot && sink_.trees.size() == 2;
  }));
  EXPECT_EQ(plugin.launch_path(), ProcessPlugin::LaunchPath::kPlain);
  EXPECT_EQ(*sink_.snapshot->FindString("greeting"), "plain");
  base::test::TestFuture<int> exit_code;
  plugin.Shutdown(exit_code.GetCallback());
  EXPECT_EQ(exit_code.Get(), 0);
}

}  // namespace
}  // namespace views_shell
