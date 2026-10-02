// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The transport against a scripted server on a real AF_UNIX socket: the
// client connects for real, frames for real, and reads on its own IO thread.
// The adapter's transcript replay lives in scroll_adapter_unittest.cc.

#include "views_shell/wm/adapters/scroll/scroll_ipc_client.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/cstring_view.h"
#include "base/synchronization/lock.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell::scroll {
namespace {

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
  base::span(raw).copy_from(base::as_byte_span(bytes).subspan(offset).first<4>());
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

// Accepts connections and runs `handler` for each on its own thread, with the
// connection's first frame already read. The handler owns nothing: the
// server closes every connection when it is destroyed.
class ScriptedServer {
 public:
  using Handler = base::RepeatingCallback<void(int fd, Frame first)>;

  ScriptedServer(const base::FilePath& path, Handler handler)
      : handler_(std::move(handler)) {
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
    accept_thread_ = std::thread(&ScriptedServer::AcceptLoop, this);
  }

  ~ScriptedServer() {
    stop_ = true;
    accept_thread_.join();
    {
      base::AutoLock lock(lock_);
      for (int fd : fds_) {
        ::shutdown(fd, SHUT_RDWR);
      }
    }
    for (std::thread& thread : threads_) {
      thread.join();
    }
    base::AutoLock lock(lock_);
    for (int fd : fds_) {
      ::close(fd);
    }
  }

  // Closes every connection accepted so far.
  void CloseAll() {
    base::AutoLock lock(lock_);
    for (int fd : fds_) {
      ::shutdown(fd, SHUT_RDWR);
    }
  }

  void Record(bool events, const Frame& frame) {
    base::AutoLock lock(lock_);
    received_.emplace_back(events, frame);
  }

  std::vector<std::pair<bool, Frame>> received() {
    base::AutoLock lock(lock_);
    return received_;
  }

 private:
  void AcceptLoop() {
    while (!stop_) {
      struct pollfd pfd = {listen_fd_.get(), POLLIN, 0};
      if (::poll(&pfd, 1, 50) <= 0) {
        continue;
      }
      const int fd = ::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
      if (fd < 0) {
        continue;
      }
      base::AutoLock lock(lock_);
      fds_.push_back(fd);
      threads_.emplace_back([this, fd] {
        std::optional<Frame> first = ReadFrame(fd);
        if (first) {
          handler_.Run(fd, std::move(*first));
        }
      });
    }
  }

  Handler handler_;
  base::ScopedFD listen_fd_;
  std::atomic<bool> stop_{false};
  std::thread accept_thread_;
  base::Lock lock_;
  std::vector<std::thread> threads_;
  std::vector<int> fds_;
  std::vector<std::pair<bool, Frame>> received_;
};

class RecordingObserver : public ScrollIpcClient::Observer {
 public:
  void OnConnected() override { ++connected; }
  void OnEvent(uint32_t type, base::Value payload) override {
    events.emplace_back(type, std::move(payload));
  }
  void OnDisconnected() override { ++disconnected; }

  int connected = 0;
  int disconnected = 0;
  std::vector<std::pair<uint32_t, base::Value>> events;
};

class ScrollIpcClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    socket_path_ = temp_dir_.GetPath().AppendASCII("scroll-ipc.test.sock");
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath socket_path_;
};

std::string Event(uint32_t type, std::string_view json) {
  return BuildIpcFrame(type, json);
}

TEST_F(ScrollIpcClientTest, ReassemblesSplitAndCoalescedEventFrames) {
  ScriptedServer* server_ptr = nullptr;
  ScriptedServer server(
      socket_path_,
      base::BindRepeating(
          [](ScriptedServer** server, int fd, Frame first) {
            (*server)->Record(first.type == kIpcSubscribe, first);
            if (first.type != kIpcSubscribe) {
              while (ReadFrame(fd)) {
              }
              return;
            }
            WriteAll(fd, BuildIpcFrame(kIpcSubscribe, R"({"success":true})"));
            // One frame in three writes: inside the magic, inside the
            // payload, and the rest.
            const std::string split =
                Event(kIpcWorkspaceEvent, R"({"change":"focus","n":1})");
            WriteAll(fd, std::string_view(split).substr(0, 3));
            base::PlatformThread::Sleep(base::Milliseconds(30));
            WriteAll(fd, std::string_view(split).substr(3, 15));
            base::PlatformThread::Sleep(base::Milliseconds(30));
            WriteAll(fd, std::string_view(split).substr(18));
            // Two frames in one write.
            WriteAll(fd, Event(kIpcWindowEvent, R"({"change":"new","n":2})") +
                             Event(kIpcTickEvent, R"({"first":false,"n":3})"));
            while (ReadFrame(fd)) {
            }
          },
          &server_ptr));
  server_ptr = &server;

  RecordingObserver observer;
  ScrollIpcClient client(socket_path_.value(), {"workspace", "window", "tick"});
  client.Start(&observer);
  ASSERT_TRUE(base::test::RunUntil([&] { return observer.events.size() == 3; }));
  EXPECT_EQ(1, observer.connected);
  EXPECT_EQ(0, observer.disconnected);
  const uint32_t types[] = {kIpcWorkspaceEvent, kIpcWindowEvent, kIpcTickEvent};
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(types[i], observer.events[i].first);
    ASSERT_TRUE(observer.events[i].second.is_dict());
    EXPECT_EQ(static_cast<int>(i + 1),
              observer.events[i].second.GetDict().FindInt("n"));
  }
  // SUBSCRIBE carried the topics as a JSON array.
  bool saw_subscribe = false;
  for (const auto& [events, frame] : server.received()) {
    if (events && frame.type == kIpcSubscribe) {
      saw_subscribe = true;
      EXPECT_EQ(R"(["workspace","window","tick"])", frame.payload);
    }
  }
  EXPECT_TRUE(saw_subscribe);
}

TEST_F(ScrollIpcClientTest, AnswersPipelinedRequestsInOrder) {
  ScriptedServer* server_ptr = nullptr;
  ScriptedServer server(
      socket_path_,
      base::BindRepeating(
          [](ScriptedServer** server, int fd, Frame first) {
            if (first.type == kIpcSubscribe) {
              (*server)->Record(true, first);
              WriteAll(fd, BuildIpcFrame(kIpcSubscribe, R"({"success":true})"));
              while (ReadFrame(fd)) {
              }
              return;
            }
            std::optional<Frame> frame = std::move(first);
            while (frame) {
              (*server)->Record(false, *frame);
              std::string reply;
              switch (frame->type) {
                case kIpcGetVersion:
                  reply = R"({"variant":"scroll","human_readable":"1.13"})";
                  break;
                case kIpcGetWorkspaces:
                  reply = R"([{"id":4,"name":"1"}])";
                  break;
                case kIpcRunCommand:
                  reply = R"([{"success":true}])";
                  break;
                default:
                  reply = "not json";
              }
              WriteAll(fd, BuildIpcFrame(frame->type, reply));
              frame = ReadFrame(fd);
            }
          },
          &server_ptr));
  server_ptr = &server;

  RecordingObserver observer;
  ScrollIpcClient client(socket_path_.value(), {"workspace"});
  client.Start(&observer);
  // Sent before OnConnected: they wait for the socket.
  base::test::TestFuture<std::optional<base::Value>> version;
  base::test::TestFuture<std::optional<base::Value>> workspaces;
  base::test::TestFuture<std::optional<base::Value>> command;
  base::test::TestFuture<std::optional<base::Value>> garbage;
  client.Request(kIpcGetVersion, std::string(), version.GetCallback());
  client.Request(kIpcGetWorkspaces, std::string(), workspaces.GetCallback());
  client.Request(kIpcRunCommand, "workspace 3", command.GetCallback());
  client.Request(kIpcGetTree, std::string(), garbage.GetCallback());

  ASSERT_TRUE(version.Wait());
  ASSERT_TRUE(version.Get().has_value());
  EXPECT_EQ("scroll", *version.Get()->GetDict().FindString("variant"));
  ASSERT_TRUE(workspaces.Wait());
  ASSERT_TRUE(workspaces.Get().has_value());
  EXPECT_EQ(1u, workspaces.Get()->GetList().size());
  ASSERT_TRUE(command.Wait());
  ASSERT_TRUE(command.Get().has_value());
  EXPECT_TRUE(command.Get()->GetList()[0].GetDict().FindBool("success").value());
  // A reply that is not JSON answers nullopt, and the stream goes on.
  ASSERT_TRUE(garbage.Wait());
  EXPECT_FALSE(garbage.Get().has_value());
  EXPECT_EQ(0, observer.disconnected);

  // RUN_COMMAND's payload is the raw text, not JSON; queries carry nothing.
  std::vector<std::pair<uint32_t, std::string>> requests;
  for (const auto& [events, frame] : server.received()) {
    if (!events) {
      requests.emplace_back(frame.type, frame.payload);
    }
  }
  ASSERT_EQ(4u, requests.size());
  EXPECT_EQ(std::make_pair(uint32_t{kIpcGetVersion}, std::string()),
            requests[0]);
  EXPECT_EQ(std::make_pair(uint32_t{kIpcGetWorkspaces}, std::string()),
            requests[1]);
  EXPECT_EQ(std::make_pair(uint32_t{kIpcRunCommand}, std::string("workspace 3")),
            requests[2]);
}

TEST_F(ScrollIpcClientTest, AnswersPendingRequestsWhenTheServerCloses) {
  ScriptedServer* server_ptr = nullptr;
  ScriptedServer server(
      socket_path_,
      base::BindRepeating(
          [](ScriptedServer** server, int fd, Frame first) {
            if (first.type == kIpcSubscribe) {
              WriteAll(fd, BuildIpcFrame(kIpcSubscribe, R"({"success":true})"));
              while (ReadFrame(fd)) {
              }
              return;
            }
            // Never answer: close everything on the first request.
            (*server)->CloseAll();
          },
          &server_ptr));
  server_ptr = &server;

  RecordingObserver observer;
  ScrollIpcClient client(socket_path_.value(), {"workspace"});
  client.Start(&observer);
  base::test::TestFuture<std::optional<base::Value>> tree;
  client.Request(kIpcGetTree, std::string(), tree.GetCallback());
  ASSERT_TRUE(tree.Wait());
  EXPECT_FALSE(tree.Get().has_value());
  ASSERT_TRUE(base::test::RunUntil([&] { return observer.disconnected == 1; }));

  // Dead for good: a later request is answered nullopt, and nothing repeats.
  base::test::TestFuture<std::optional<base::Value>> later;
  client.Request(kIpcGetVersion, std::string(), later.GetCallback());
  ASSERT_TRUE(later.Wait());
  EXPECT_FALSE(later.Get().has_value());
  EXPECT_EQ(1, observer.disconnected);
}

TEST_F(ScrollIpcClientTest, ReportsAMissingSocketAsDisconnected) {
  RecordingObserver observer;
  ScrollIpcClient client(socket_path_.value(), {"workspace"});
  client.Start(&observer);
  ASSERT_TRUE(base::test::RunUntil([&] { return observer.disconnected == 1; }));
  EXPECT_EQ(0, observer.connected);
}

class FakeEnvironment : public base::Environment {
 public:
  std::optional<std::string> GetVar(base::cstring_view name) override {
    auto it = vars.find(std::string(name));
    if (it == vars.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  std::map<std::string, std::string> vars;
};

TEST_F(ScrollIpcClientTest, ResolvesTheSocketInScrollOrder) {
  FakeEnvironment env;
  std::string socketpath_output;
  auto get_socketpath = base::BindLambdaForTesting(
      [&]() { return socketpath_output; });

  EXPECT_EQ("", ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));

  // The $XDG_RUNTIME_DIR scan, scroll before sway.
  env.vars["XDG_RUNTIME_DIR"] = temp_dir_.GetPath().value();
  const base::FilePath sway = temp_dir_.GetPath().AppendASCII("sway-ipc.1000.7.sock");
  ASSERT_TRUE(base::WriteFile(sway, ""));
  EXPECT_EQ(sway.value(),
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
  const base::FilePath scroll =
      temp_dir_.GetPath().AppendASCII("scroll-ipc.1000.9.sock");
  ASSERT_TRUE(base::WriteFile(scroll, ""));
  EXPECT_EQ(scroll.value(),
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));

  env.vars["I3SOCK"] = "/run/i3.sock";
  EXPECT_EQ("/run/i3.sock",
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
  env.vars["SWAYSOCK"] = "/run/sway.sock";
  EXPECT_EQ("/run/sway.sock",
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
  socketpath_output = "/run/from-get-socketpath.sock";
  EXPECT_EQ("/run/from-get-socketpath.sock",
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
  env.vars["SCROLLSOCK"] = "/run/scroll.sock";
  EXPECT_EQ("/run/scroll.sock",
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
  // An empty variable counts as unset.
  env.vars["SCROLLSOCK"] = "";
  EXPECT_EQ("/run/from-get-socketpath.sock",
            ScrollIpcClient::ResolveSocketPathFrom(env, get_socketpath));
}

}  // namespace
}  // namespace views_shell::scroll
