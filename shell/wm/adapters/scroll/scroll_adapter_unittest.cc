// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The scroll adapter against a fake compositor that replays
// testdata/scroll-transcript.jsonl (recorded from headless stock scroll on the
// bench by testdata/record_transcript.py; see testdata/README.md) on a real
// AF_UNIX socket.
//
// The fake replays by epoch. Epoch 0 is the state before the first recorded
// RUN_COMMAND; each RUN_COMMAND opens the next epoch. A query (GET_OUTPUTS,
// GET_WORKSPACES, GET_TREE, GET_VERSION) is answered with the last reply of
// its type recorded in the current epoch (or the nearest earlier one), so the
// adapter may query as often as it likes. A RUN_COMMAND must be the epoch's
// recorded command, byte for byte; the fake writes the epoch's events on the
// events connection (each one split across two writes) and then the recorded
// reply. A SEND_TICK is answered with a `tick` event carrying the adapter's
// payload, then {"success": true}, which is what scroll does.

#include "views_shell/wm/adapters/scroll/scroll_adapter.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/numerics/byte_conversions.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell::scroll {
namespace {

using Result = base::expected<void, WmCommandError>;

// --- The transcript, folded into epochs -------------------------------------

struct Epoch {
  std::string command;  // empty for epoch 0
  std::optional<std::string> command_reply;
  std::vector<std::pair<uint32_t, std::string>> events;  // ticks excluded
  std::map<uint32_t, std::string> replies;               // last per type
};

struct Transcript {
  std::vector<std::string> subscriptions;
  std::string subscribe_reply;
  std::vector<Epoch> epochs;
};

std::string ToWire(const base::Value& payload) {
  if (payload.is_none()) {
    return std::string();
  }
  if (payload.is_string()) {
    return payload.GetString();  // RUN_COMMAND and SEND_TICK text
  }
  return base::WriteJson(payload).value_or(std::string());
}

Transcript ParseTranscript(std::string_view jsonl) {
  Transcript transcript;
  transcript.epochs.emplace_back();
  for (std::string_view line : base::SplitStringPiece(
           jsonl, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::optional<base::Value> value = base::JSONReader::Read(line);
    CHECK(value && value->is_dict()) << line;
    const base::DictValue& dict = value->GetDict();
    const bool c2s = *dict.FindString("dir") == "c2s";
    const bool events = *dict.FindString("conn") == "events";
    const uint32_t type = static_cast<uint32_t>(*dict.FindDouble("type"));
    const base::Value& payload = *dict.Find("payload");
    Epoch& epoch = transcript.epochs.back();
    if (c2s) {
      if (events && type == kIpcSubscribe) {
        for (const base::Value& topic : payload.GetList()) {
          transcript.subscriptions.push_back(topic.GetString());
        }
      } else if (type == kIpcRunCommand) {
        transcript.epochs.emplace_back().command = payload.GetString();
      }
      continue;
    }
    if (events) {
      if (type == kIpcSubscribe) {
        transcript.subscribe_reply = ToWire(payload);
      } else if (type != kIpcTickEvent) {
        epoch.events.emplace_back(type, ToWire(payload));
      }
      continue;
    }
    if (type == kIpcRunCommand) {
      epoch.command_reply = ToWire(payload);
    } else if (type != kIpcSendTick) {
      epoch.replies[type] = ToWire(payload);
    }
  }
  return transcript;
}

std::string LoadRecordedTranscript() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string content;
  CHECK(base::ReadFileToString(
      root.AppendASCII(
          "views_shell/wm/adapters/scroll/testdata/scroll-transcript.jsonl"),
      &content));
  return content;
}

// --- The fake compositor ------------------------------------------------------

struct Frame {
  uint32_t type = 0;
  std::string payload;
};

bool WriteAll(int fd, std::string_view data) {
  while (!data.empty()) {
    const ssize_t n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    if (n <= 0) {
      return false;
    }
    data.remove_prefix(static_cast<size_t>(n));
  }
  return true;
}

bool ReadExactly(int fd, std::string& out, size_t n) {
  out.resize(n);
  size_t got = 0;
  while (got < n) {
    const ssize_t r =
        ::recv(fd, base::span(out).subspan(got).data(), n - got, 0);
    if (r <= 0) {
      return false;
    }
    got += static_cast<size_t>(r);
  }
  return true;
}

uint32_t U32At(const std::string& bytes, size_t offset) {
  std::array<uint8_t, 4> raw;
  base::span(raw).copy_from(
      base::as_byte_span(bytes).subspan(offset).first<4>());
  return base::U32FromNativeEndian(raw);
}

std::optional<Frame> ReadFrame(int fd) {
  std::string header;
  if (!ReadExactly(fd, header, kIpcHeaderLength) ||
      header.substr(0, 6) != "i3-ipc") {
    return std::nullopt;
  }
  Frame frame;
  frame.type = U32At(header, 10);
  if (!ReadExactly(fd, frame.payload, U32At(header, 6))) {
    return std::nullopt;
  }
  return frame;
}

class FakeCompositor {
 public:
  struct Options {
    // Close every connection, unanswered, on the first GET_TREE.
    bool drop_on_first_tree = false;
    // Answer SEND_TICK without the tick event.
    bool swallow_ticks = false;
  };

  FakeCompositor(const base::FilePath& path,
                 Transcript transcript,
                 Options options)
      : transcript_(std::move(transcript)), options_(options) {
    listen_fd_.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    CHECK(listen_fd_.is_valid());
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    base::span<char> sun_path(addr.sun_path);
    CHECK_LT(path.value().size(), sun_path.size());
    sun_path.first(path.value().size()).copy_from(base::span(path.value()));
    CHECK_EQ(::bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr),
                    sizeof(addr)),
             0);
    CHECK_EQ(::listen(listen_fd_.get(), 8), 0);
    accept_thread_ = std::thread(&FakeCompositor::AcceptLoop, this);
  }

  ~FakeCompositor() {
    stop_ = true;
    accept_thread_.join();
    CloseAll();
    for (std::thread& thread : threads_) {
      thread.join();
    }
    base::AutoLock lock(lock_);
    for (int fd : fds_) {
      ::close(fd);
    }
  }

  // What the fake read, in order: (events connection?, frame).
  std::vector<std::pair<bool, Frame>> received() {
    base::AutoLock lock(lock_);
    return received_;
  }
  std::vector<std::string> mismatches() {
    base::AutoLock lock(lock_);
    return mismatches_;
  }
  int accepted() {
    base::AutoLock lock(lock_);
    return accepted_;
  }

 private:
  void CloseAll() {
    base::AutoLock lock(lock_);
    for (int fd : fds_) {
      ::shutdown(fd, SHUT_RDWR);
    }
    events_fd_ = -1;
  }

  void AcceptLoop() {
    while (!stop_) {
      struct pollfd pfd = {listen_fd_.get(), POLLIN, 0};
      if (::poll(&pfd, 1, 20) <= 0) {
        continue;
      }
      const int fd =
          ::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
      if (fd < 0) {
        continue;
      }
      base::AutoLock lock(lock_);
      ++accepted_;
      fds_.push_back(fd);
      if (gone_) {
        ::shutdown(fd, SHUT_RDWR);  // the compositor exited
        continue;
      }
      threads_.emplace_back(&FakeCompositor::Serve, this, fd);
    }
  }

  void Record(bool events, const Frame& frame) {
    base::AutoLock lock(lock_);
    received_.emplace_back(events, frame);
  }

  // Each event goes out in two writes, split inside the payload.
  void WriteEvent(uint32_t type, std::string_view json) {
    base::AutoLock lock(write_lock_);
    int fd;
    {
      base::AutoLock state(lock_);
      fd = events_fd_;
    }
    if (fd < 0) {
      return;
    }
    const std::string frame = BuildIpcFrame(type, json);
    const size_t cut = kIpcHeaderLength + json.size() / 2;
    WriteAll(fd, std::string_view(frame).substr(0, cut));
    base::PlatformThread::Sleep(base::Milliseconds(2));
    WriteAll(fd, std::string_view(frame).substr(cut));
  }

  const std::string* Reply(uint32_t type) {
    for (size_t e = epoch_ + 1; e-- > 0;) {
      auto it = transcript_.epochs[e].replies.find(type);
      if (it != transcript_.epochs[e].replies.end()) {
        return &it->second;
      }
    }
    for (const Epoch& epoch : transcript_.epochs) {
      auto it = epoch.replies.find(type);
      if (it != epoch.replies.end()) {
        return &it->second;
      }
    }
    return nullptr;
  }

  void Serve(int fd) {
    std::optional<Frame> frame = ReadFrame(fd);
    if (!frame) {
      return;
    }
    if (frame->type == kIpcSubscribe) {
      Record(true, *frame);
      WriteAll(fd, BuildIpcFrame(kIpcSubscribe, transcript_.subscribe_reply));
      {
        base::AutoLock lock(lock_);
        events_fd_ = fd;
      }
      WriteEvent(kIpcTickEvent, R"({"first":true,"payload":""})");
      for (const auto& [type, json] : transcript_.epochs[0].events) {
        WriteEvent(type, json);
      }
      while (ReadFrame(fd)) {
      }
      return;
    }
    while (frame) {
      Record(false, *frame);
      switch (frame->type) {
        case kIpcRunCommand:
          RunCommand(fd, frame->payload);
          break;
        case kIpcSendTick:
          if (!options_.swallow_ticks) {
            base::DictValue tick;
            tick.Set("first", false).Set("payload", frame->payload);
            WriteEvent(kIpcTickEvent, base::WriteJson(tick).value());
          }
          WriteAll(fd, BuildIpcFrame(kIpcSendTick, R"({"success":true})"));
          break;
        default: {
          if (frame->type == kIpcGetTree && options_.drop_on_first_tree &&
              !dropped_) {
            dropped_ = true;
            CloseAll();
            return;
          }
          const std::string* reply = Reply(frame->type);
          WriteAll(fd, BuildIpcFrame(frame->type, reply ? *reply : "{}"));
        }
      }
      frame = ReadFrame(fd);
    }
  }

  void RunCommand(int fd, const std::string& command) {
    if (epoch_ + 1 >= transcript_.epochs.size()) {
      base::AutoLock lock(lock_);
      mismatches_.push_back("no recorded epoch for: " + command);
      return;
    }
    const Epoch& epoch = transcript_.epochs[++epoch_];
    if (epoch.command != command) {
      base::AutoLock lock(lock_);
      mismatches_.push_back("expected `" + epoch.command + "`, got `" +
                            command + "`");
    }
    for (const auto& [type, json] : epoch.events) {
      WriteEvent(type, json);
    }
    if (epoch.command_reply) {
      WriteAll(fd, BuildIpcFrame(kIpcRunCommand, *epoch.command_reply));
    }
    if (command == "exit") {
      {
        base::AutoLock lock(lock_);
        gone_ = true;
      }
      CloseAll();
    }
  }

  const Transcript transcript_;
  const Options options_;
  base::ScopedFD listen_fd_;
  std::atomic<bool> stop_{false};
  std::thread accept_thread_;
  std::vector<std::thread> threads_;  // touched by the accept thread only
  // The requests connection is served by one thread at a time.
  size_t epoch_ = 0;
  bool dropped_ = false;
  base::Lock write_lock_;
  base::Lock lock_;
  int events_fd_ = -1;
  bool gone_ = false;
  int accepted_ = 0;
  std::vector<int> fds_;
  std::vector<std::pair<bool, Frame>> received_;
  std::vector<std::string> mismatches_;
};

// --- The delegate under test --------------------------------------------------

class TestDelegate : public CompositorAdapter::Delegate {
 public:
  void OnSnapshot(WmSnapshot snapshot) override {
    snapshots.push_back(std::move(snapshot));
  }
  void OnBinding(std::string_view payload) override {
    bindings.emplace_back(payload);
  }
  void OnConfigReloaded(bool ok, std::string_view error) override {
    reloads.emplace_back(ok, std::string(error));
  }
  void OnDisconnected() override { ++disconnects; }

  const WmSnapshot& last() const { return snapshots.back(); }
  std::string FocusedName() const {
    if (snapshots.empty() || !last().FocusedWorkspace()) {
      return std::string();
    }
    return last().FocusedWorkspace()->name;
  }

  std::vector<WmSnapshot> snapshots;
  std::vector<std::string> bindings;
  std::vector<std::pair<bool, std::string>> reloads;
  int disconnects = 0;
};

struct Outcome {
  Result result;
  // What the delegate held when `done` ran.
  size_t snapshots_at_done = 0;
  std::string focused_at_done;
};

Outcome SendAndWait(ScrollAdapter& adapter,
                    TestDelegate& delegate,
                    const WmCommand& command) {
  base::test::TestFuture<Outcome> future;
  adapter.Send(command,
               base::BindOnce(
                   [](TestDelegate* delegate,
                      base::OnceCallback<void(Outcome)> callback,
                      Result result) {
                     std::move(callback).Run(
                         Outcome{result, delegate->snapshots.size(),
                                 delegate->FocusedName()});
                   },
                   &delegate, future.GetCallback()));
  return future.Take();
}

WmCommand Command(WmCommand::Kind kind,
                  std::string workspace = std::string(),
                  std::string window = std::string(),
                  std::string name = std::string()) {
  WmCommand command(kind);
  command.workspace = std::move(workspace);
  command.window = std::move(window);
  command.name = std::move(name);
  return command;
}

class ScrollAdapterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    socket_path_ = temp_dir_.GetPath().AppendASCII("scroll-ipc.test.sock");
  }

  std::unique_ptr<ScrollAdapter> MakeAdapter() {
    ScrollAdapter::Options options;
    options.socket_path = socket_path_.value();
    options.initial_backoff = base::Milliseconds(10);
    options.max_backoff = base::Milliseconds(40);
    options.command_timeout = base::Seconds(3);
    return std::make_unique<ScrollAdapter>(options);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath socket_path_;
};

TEST_F(ScrollAdapterTest, SubscribesFirstThenPublishesTheRecordedState) {
  const Transcript transcript = ParseTranscript(LoadRecordedTranscript());
  // The recording must carry what the replay below relies on.
  ASSERT_EQ(AdapterSubscriptions(), transcript.subscriptions);
  ASSERT_GE(transcript.epochs.size(), 9u);

  FakeCompositor fake(socket_path_, transcript, {});
  TestDelegate delegate;
  std::unique_ptr<ScrollAdapter> adapter = MakeAdapter();
  adapter->Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));

  // Subscribe on the events connection, then GET_VERSION, then one refresh.
  const auto received = fake.received();
  ASSERT_GE(received.size(), 5u);
  EXPECT_TRUE(received[0].first);
  EXPECT_EQ(uint32_t{kIpcSubscribe}, received[0].second.type);
  EXPECT_EQ(R"(["workspace","window","output","binding","shutdown","tick"])",
            received[0].second.payload);
  const uint32_t order[] = {kIpcGetVersion, kIpcGetOutputs, kIpcGetWorkspaces,
                            kIpcGetTree};
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_FALSE(received[i + 1].first);
    EXPECT_EQ(order[i], received[i + 1].second.type) << i;
  }

  EXPECT_EQ("scroll", adapter->name());
  EXPECT_TRUE(adapter->connected());
  const CapabilitySet& capabilities = adapter->capabilities();
  for (const char* yes : {"workspaces.focus", "windows.move-to-workspace",
                          "bindings.events", "scroll.scroller",
                          "session.exit"}) {
    EXPECT_TRUE(capabilities.contains(yes)) << yes;
  }
  // Stock scroll has no GET_VERSION features array: no hook rows.
  for (const char* no : {"overview.events", "windows.geometry-events",
                         "bindings.gesture-events"}) {
    EXPECT_FALSE(capabilities.contains(no)) << no;
  }

  const WmSnapshot& snapshot = delegate.last();
  ASSERT_EQ(1u, snapshot.outputs.size());
  EXPECT_EQ("HEADLESS-1", snapshot.outputs[0].id);
  EXPECT_EQ(1920, snapshot.outputs[0].width_px);
  EXPECT_EQ(1080, snapshot.outputs[0].height_px);
  EXPECT_TRUE(snapshot.outputs[0].focused);
  ASSERT_EQ(1u, snapshot.workspaces.size());
  EXPECT_EQ("1", snapshot.workspaces[0].name);
  EXPECT_EQ("HEADLESS-1", snapshot.workspaces[0].output);
  EXPECT_TRUE(snapshot.workspaces[0].focused);
  EXPECT_TRUE(snapshot.workspaces[0].active);
  ASSERT_EQ(1u, snapshot.windows.size());
  const WmWindow& window = snapshot.windows[0];
  EXPECT_EQ(snapshot.workspaces[0].id, window.workspace);
  EXPECT_FALSE(window.app_id.empty());
  EXPECT_TRUE(window.focused);
  EXPECT_EQ(std::optional<int>(0), window.column);  // scroll's first column
  EXPECT_EQ(std::vector<std::string>{window.id}, snapshot.mru);
}

TEST_F(ScrollAdapterTest, EveryCommandLandsAfterItsEcho) {
  FakeCompositor fake(socket_path_, ParseTranscript(LoadRecordedTranscript()),
                      {});
  TestDelegate delegate;
  std::unique_ptr<ScrollAdapter> adapter = MakeAdapter();
  adapter->Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));
  const std::string window = delegate.last().windows.at(0).id;
  const std::string first_workspace = delegate.last().workspaces.at(0).id;

  // 1. Focus "3" by name: it does not exist yet, scroll creates it.
  Outcome focus = SendAndWait(*adapter, delegate,
                              Command(WmCommand::Kind::kFocusWorkspace, "", "",
                                      "3"));
  ASSERT_TRUE(focus.result.has_value());
  EXPECT_EQ("3", focus.focused_at_done);  // the echo came before `done`
  const std::string three = delegate.last().FocusedWorkspace()->id;
  EXPECT_NE(first_workspace, three);

  // 2. Rename it by id.
  Outcome rename = SendAndWait(
      *adapter, delegate,
      Command(WmCommand::Kind::kRenameWorkspace, three, "", "web"));
  ASSERT_TRUE(rename.result.has_value());
  EXPECT_EQ("web", rename.focused_at_done);

  // 3. Move the window there.
  Outcome move = SendAndWait(
      *adapter, delegate,
      Command(WmCommand::Kind::kMoveWindowToWorkspace, three, window));
  ASSERT_TRUE(move.result.has_value());
  ASSERT_TRUE(delegate.last().FindWindow(window));
  EXPECT_EQ(three, delegate.last().FindWindow(window)->workspace);

  // 4. Focus the window.
  Outcome focus_window = SendAndWait(
      *adapter, delegate, Command(WmCommand::Kind::kFocusWindow, "", window));
  ASSERT_TRUE(focus_window.result.has_value());
  EXPECT_TRUE(delegate.last().FindWindow(window)->focused);
  EXPECT_EQ(window, delegate.last().mru.at(0));

  // 5. scratchpad show on an empty scratchpad: refused, no echo wait.
  const size_t before = delegate.snapshots.size();
  Outcome scratchpad = SendAndWait(
      *adapter, delegate, Command(WmCommand::Kind::kToggleScratchpad));
  ASSERT_FALSE(scratchpad.result.has_value());
  EXPECT_EQ(WmCommandError::kRejected, scratchpad.result.error());
  EXPECT_EQ(before, scratchpad.snapshots_at_done);

  // 6. Back to "1", by id when the snapshot still lists it.
  const WmWorkspace* one = nullptr;
  for (const WmWorkspace& workspace : delegate.last().workspaces) {
    if (workspace.name == "1") {
      one = &workspace;
    }
  }
  Outcome back = SendAndWait(
      *adapter, delegate,
      one ? Command(WmCommand::Kind::kFocusWorkspace, one->id)
          : Command(WmCommand::Kind::kFocusWorkspace, "", "", "1"));
  ASSERT_TRUE(back.result.has_value());
  EXPECT_EQ("1", back.focused_at_done);

  // 7. Reload: the workspace `reload` event is relayed.
  Outcome reload =
      SendAndWait(*adapter, delegate, Command(WmCommand::Kind::kReloadConfig));
  ASSERT_TRUE(reload.result.has_value());
  ASSERT_EQ(1u, delegate.reloads.size());
  EXPECT_TRUE(delegate.reloads[0].first);

  // 8. Exit: echoed by the shutdown event, then the connection is lost.
  Outcome exit =
      SendAndWait(*adapter, delegate, Command(WmCommand::Kind::kSessionExit));
  ASSERT_TRUE(exit.result.has_value());
  ASSERT_TRUE(base::test::RunUntil([&] { return delegate.disconnects == 1; }));
  EXPECT_FALSE(adapter->connected());

  // Every command string matched the recording, and every accepted command
  // was followed by its SEND_TICK.
  EXPECT_EQ(std::vector<std::string>(), fake.mismatches());
  std::vector<uint32_t> commands;
  for (const auto& [events, frame] : fake.received()) {
    if (!events &&
        (frame.type == kIpcRunCommand || frame.type == kIpcSendTick)) {
      commands.push_back(frame.type);
    }
  }
  const std::vector<uint32_t> expected = {
      kIpcRunCommand, kIpcSendTick,   kIpcRunCommand, kIpcSendTick,
      kIpcRunCommand, kIpcSendTick,   kIpcRunCommand, kIpcSendTick,
      kIpcRunCommand, kIpcRunCommand, kIpcSendTick,   kIpcRunCommand,
      kIpcSendTick,   kIpcRunCommand};
  EXPECT_EQ(expected, commands);
}

TEST_F(ScrollAdapterTest, ReconnectsMidSnapshotAndResyncs) {
  FakeCompositor::Options options;
  options.drop_on_first_tree = true;
  FakeCompositor fake(socket_path_, ParseTranscript(LoadRecordedTranscript()),
                      options);
  TestDelegate delegate;
  std::unique_ptr<ScrollAdapter> adapter = MakeAdapter();
  adapter->Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));

  // The first pair was dropped before GET_TREE was answered; the second
  // delivered the whole state.
  EXPECT_EQ(4, fake.accepted());
  EXPECT_EQ(1u, delegate.snapshots.size());
  EXPECT_EQ(1, delegate.disconnects);
  EXPECT_EQ(1u, delegate.last().windows.size());
  EXPECT_EQ("1", delegate.FocusedName());
  int subscribes = 0;
  for (const auto& [events, frame] : fake.received()) {
    subscribes += events && frame.type == kIpcSubscribe;
  }
  EXPECT_EQ(2, subscribes);

  // Commands work on the new connection.
  Outcome focus = SendAndWait(
      *adapter, delegate,
      Command(WmCommand::Kind::kFocusWorkspace, "", "", "3"));
  ASSERT_TRUE(focus.result.has_value());
  EXPECT_EQ("3", focus.focused_at_done);
}

TEST_F(ScrollAdapterTest, FailsWithNoEchoWhenTheTickNeverComes) {
  FakeCompositor::Options options;
  options.swallow_ticks = true;
  FakeCompositor fake(socket_path_, ParseTranscript(LoadRecordedTranscript()),
                      options);
  TestDelegate delegate;
  ScrollAdapter::Options adapter_options;
  adapter_options.socket_path = socket_path_.value();
  adapter_options.command_timeout = base::Milliseconds(300);
  ScrollAdapter adapter(adapter_options);
  adapter.Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));

  Outcome focus = SendAndWait(
      adapter, delegate, Command(WmCommand::Kind::kFocusWorkspace, "", "", "3"));
  ASSERT_FALSE(focus.result.has_value());
  EXPECT_EQ(WmCommandError::kNoEcho, focus.result.error());
  // The events still arrived and were folded; only the barrier is missing.
  EXPECT_EQ("3", delegate.FocusedName());
}

TEST_F(ScrollAdapterTest, DispatchesOnlyViewsShellBindings) {
  constexpr std::string_view kSession = R"(
{"dir":"c2s","conn":"events","type":2,"payload":["workspace","window","output","binding","shutdown","tick"]}
{"dir":"s2c","conn":"events","type":2,"payload":{"success":true}}
{"dir":"s2c","conn":"events","type":2147483653,"payload":{"change":"run","binding":{"command":"exec foot"}}}
{"dir":"s2c","conn":"events","type":2147483653,"payload":{"change":"run","binding":{"command":"nop views-shell launcher toggle"}}}
{"dir":"s2c","conn":"events","type":2147483653,"payload":{"change":"run","binding":{"command":"nop views-shellx"}}}
{"dir":"s2c","conn":"requests","type":7,"payload":{"human_readable":"1.11","major":1,"minor":11,"patch":0}}
{"dir":"s2c","conn":"requests","type":3,"payload":[]}
{"dir":"s2c","conn":"requests","type":1,"payload":[]}
{"dir":"s2c","conn":"requests","type":4,"payload":{"id":1,"type":"root","nodes":[],"floating_nodes":[],"focus":[]}}
)";
  FakeCompositor fake(socket_path_, ParseTranscript(kSession), {});
  TestDelegate delegate;
  std::unique_ptr<ScrollAdapter> adapter = MakeAdapter();
  adapter->Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));
  EXPECT_EQ(std::vector<std::string>{"launcher toggle"}, delegate.bindings);
  // No `variant`: this is sway, without the scroll rows.
  EXPECT_EQ("sway", adapter->name());
  EXPECT_FALSE(adapter->capabilities().contains("scroll.scroller"));
  EXPECT_TRUE(adapter->capabilities().contains("bindings.events"));
}

TEST_F(ScrollAdapterTest, RefusesWhatItCannotSpell) {
  TestDelegate delegate;
  std::unique_ptr<ScrollAdapter> adapter = MakeAdapter();
  // Not started: not connected.
  Outcome early = SendAndWait(*adapter, delegate,
                              Command(WmCommand::Kind::kReloadConfig));
  ASSERT_FALSE(early.result.has_value());
  EXPECT_EQ(WmCommandError::kNotConnected, early.result.error());

  FakeCompositor fake(socket_path_, ParseTranscript(LoadRecordedTranscript()),
                      {});
  adapter->Start(&delegate);
  ASSERT_TRUE(base::test::RunUntil([&] { return !delegate.snapshots.empty(); }));
  for (const WmCommand& command :
       {Command(WmCommand::Kind::kFocusWindow, "", "999"),
        Command(WmCommand::Kind::kFocusWindow, "", "5; exit"),
        Command(WmCommand::Kind::kFocusWorkspace),
        Command(WmCommand::Kind::kRenameWorkspace, "999", "", "x"),
        Command(WmCommand::Kind::kMoveWindowToWorkspace, "", "999", "1")}) {
    Outcome outcome = SendAndWait(*adapter, delegate, command);
    ASSERT_FALSE(outcome.result.has_value());
    EXPECT_EQ(WmCommandError::kInvalidArgument, outcome.result.error());
  }
  // Nothing reached the compositor.
  for (const auto& [events, frame] : fake.received()) {
    EXPECT_NE(uint32_t{kIpcRunCommand}, frame.type);
  }
}

TEST(ScrollAdapterStaticTest, ProbesCapabilitiesFromGetVersion) {
  std::string name;
  base::DictValue scroll;
  scroll.Set("variant", "scroll");
  CapabilitySet plain = ProbeCapabilities(scroll, &name);
  EXPECT_EQ("scroll", name);
  EXPECT_EQ(22u, plain.size());
  EXPECT_FALSE(plain.contains("overview.events"));

  // H10: a features array keeps the hook rows it names, by capability or id.
  base::ListValue features;
  features.Append("H2");
  features.Append("windows.geometry-events");
  features.Append("scroll.invented");
  scroll.Set("features", std::move(features));
  CapabilitySet hooked = ProbeCapabilities(scroll, &name);
  EXPECT_EQ(24u, hooked.size());
  EXPECT_TRUE(hooked.contains("overview.events"));
  EXPECT_TRUE(hooked.contains("windows.geometry-events"));
  EXPECT_FALSE(hooked.contains("bindings.gesture-events"));
  EXPECT_FALSE(hooked.contains("scroll.invented"));  // never invented

  base::DictValue sway;
  base::ListValue sway_features;
  sway_features.Append("H2");  // not a sway row
  sway_features.Append("h4");
  sway.Set("features", std::move(sway_features));
  CapabilitySet sway_set = ProbeCapabilities(sway, &name);
  EXPECT_EQ("sway", name);
  EXPECT_EQ(15u, sway_set.size());
  EXPECT_TRUE(sway_set.contains("bindings.gesture-events"));
  EXPECT_FALSE(sway_set.contains("overview.events"));
  EXPECT_FALSE(sway_set.contains("scroll.scroller"));
}

TEST(ScrollAdapterStaticTest, SpellsCommandsAndQuotesNames) {
  WmSnapshot snapshot;
  WmWorkspace workspace;
  workspace.id = "7";
  workspace.name = R"(we"b\1)";
  snapshot.workspaces.push_back(workspace);
  WmWindow window;
  window.id = "5";
  window.workspace = "7";
  snapshot.windows.push_back(window);

  auto text = [&](const WmCommand& command) {
    return CommandText(command, snapshot).value_or("<none>");
  };
  EXPECT_EQ(R"(workspace --no-auto-back-and-forth "we\"b\\1")",
            text(Command(WmCommand::Kind::kFocusWorkspace, "7")));
  EXPECT_EQ(R"(workspace --no-auto-back-and-forth "3")",
            text(Command(WmCommand::Kind::kFocusWorkspace, "", "", "3")));
  EXPECT_EQ("[con_id=5] focus",
            text(Command(WmCommand::Kind::kFocusWindow, "", "5")));
  EXPECT_EQ(R"(rename workspace "we\"b\\1" to "mail")",
            text(Command(WmCommand::Kind::kRenameWorkspace, "7", "", "mail")));
  EXPECT_EQ(R"([con_id=5] move container to workspace "9")",
            text(Command(WmCommand::Kind::kMoveWindowToWorkspace, "", "5",
                         "9")));
  EXPECT_EQ("scratchpad show",
            text(Command(WmCommand::Kind::kToggleScratchpad)));
  EXPECT_EQ("exit", text(Command(WmCommand::Kind::kSessionExit)));
  EXPECT_EQ("reload", text(Command(WmCommand::Kind::kReloadConfig)));
}

}  // namespace
}  // namespace views_shell::scroll
