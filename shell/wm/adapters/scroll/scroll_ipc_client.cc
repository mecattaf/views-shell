// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/wm/adapters/scroll/scroll_ipc_client.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <deque>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/message_loop/io_watcher.h"
#include "base/message_loop/message_pump_type.h"
#include "base/numerics/byte_conversions.h"
#include "base/process/launch.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"

namespace views_shell::scroll {
namespace {

constexpr std::string_view kIpcMagic = "i3-ipc";
// A payload above this is a protocol error, not a message.
constexpr uint32_t kMaxPayloadLength = 64u << 20;

std::string RunGetSocketPath() {
  std::string output;
  int exit_code = -1;
  if (!base::GetAppOutputWithExitCode(std::vector<std::string>{"scroll", "--get-socketpath"}, &output,
                                      &exit_code) ||
      exit_code != 0) {
    return std::string();
  }
  return std::string(base::TrimWhitespaceASCII(output, base::TRIM_ALL));
}

// The newest entry of `dir` that matches `pattern`, or empty.
std::string NewestMatch(const base::FilePath& dir, const std::string& pattern) {
  base::FileEnumerator files(dir, /*recursive=*/false,
                             base::FileEnumerator::FILES, pattern);
  base::FilePath best;
  base::Time best_time;
  for (base::FilePath path = files.Next(); !path.empty();
       path = files.Next()) {
    const base::Time modified = files.GetInfo().GetLastModifiedTime();
    if (best.empty() || modified > best_time) {
      best = path;
      best_time = modified;
    }
  }
  return best.value();
}

// A native-endian uint32 from the first four bytes of `bytes`.
uint32_t ReadU32(std::string_view bytes) {
  std::array<uint8_t, 4> raw;
  base::span(raw).copy_from(base::as_byte_span(bytes).first<4>());
  return base::U32FromNativeEndian(raw);
}

}  // namespace

std::string BuildIpcFrame(uint32_t type, std::string_view payload) {
  std::string frame(kIpcMagic);
  const std::array<uint8_t, 4> length =
      base::U32ToNativeEndian(static_cast<uint32_t>(payload.size()));
  const std::array<uint8_t, 4> kind = base::U32ToNativeEndian(type);
  frame.append(base::as_string_view(base::span(length)));
  frame.append(base::as_string_view(base::span(kind)));
  frame.append(payload);
  return frame;
}

// ---------------------------------------------------------------------------
// Core: everything that lives on the IO thread. Created on the Start()
// sequence, then used and destroyed on the IO thread only. Results go back
// through `ui_task_runner_` bound to a WeakPtr of the client.
// ---------------------------------------------------------------------------
class ScrollIpcClient::Core {
 public:
  Core(std::string socket_path,
       std::vector<std::string> subscriptions,
       scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
       base::WeakPtr<ScrollIpcClient> client);
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  ~Core();

  void Init();
  void Request(uint32_t type, std::string payload, ReplyCallback reply);

  // From the connections.
  void OnConnectionReady(Connection* connection);
  void OnConnectionFailed(Connection* connection);
  void OnFrame(Connection* connection, uint32_t type, std::string payload);

 private:
  void Fail();
  void PostReply(ReplyCallback reply, std::optional<base::Value> value);

  std::string socket_path_;
  std::vector<std::string> subscriptions_;
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  base::WeakPtr<ScrollIpcClient> client_;
  std::unique_ptr<Connection> requests_;
  std::unique_ptr<Connection> events_;
  std::deque<ReplyCallback> pending_;
  bool failed_ = false;
  bool subscribed_ = false;
};

// ---------------------------------------------------------------------------
// Connection: one non-blocking AF_UNIX socket with i3-ipc framing, on the IO
// thread. Writes are buffered while connecting and flushed when the socket
// takes them; reads are reassembled into whole frames.
// ---------------------------------------------------------------------------
class ScrollIpcClient::Connection : public base::IOWatcher::FdWatcher {
 public:
  Connection(std::string socket_path, Core* core, bool is_events)
      : socket_path_(std::move(socket_path)),
        core_(core),
        is_events_(is_events) {}
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection() override = default;

  bool is_events() const { return is_events_; }

  void Connect() {
    fd_.reset(
        ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!fd_.is_valid()) {
      PLOG(ERROR) << "scroll ipc: socket()";
      Fail();
      return;
    }
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    base::span<char> sun_path(addr.sun_path);
    if (socket_path_.empty() || socket_path_.size() >= sun_path.size()) {
      LOG(ERROR) << "scroll ipc: no compositor socket, or its path is too long";
      Fail();
      return;
    }
    sun_path.first(socket_path_.size()).copy_from(base::span(socket_path_));
    const int rv = ::connect(
        fd_.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rv == 0) {
      OnConnected();
      return;
    }
    if (errno == EINPROGRESS || errno == EAGAIN || errno == EINTR) {
      connecting_ = true;
      UpdateWatch();
      return;
    }
    PLOG(ERROR) << "scroll ipc: connect(" << socket_path_ << ")";
    Fail();
  }

  // Queues one framed message; it goes out as soon as the socket takes it.
  void Send(uint32_t type, std::string_view payload) {
    if (failed_) {
      return;
    }
    write_buf_ += BuildIpcFrame(type, payload);
    FlushWrites();
  }

 private:
  void OnConnected() {
    connecting_ = false;
    connected_ = true;
    core_->OnConnectionReady(this);  // may Send()
    FlushWrites();
  }

  // base::IOWatcher::FdWatcher:
  void OnFdReadable(int fd) override {
    std::array<char, 16384> buf;
    for (;;) {
      const ssize_t n = ::read(fd, buf.data(), buf.size());
      if (n > 0) {
        read_buf_.append(
            base::as_string_view(base::span(buf).first(static_cast<size_t>(n))));
        continue;
      }
      if (n == 0) {
        VLOG(1) << "scroll ipc: end of stream on the "
                << (is_events_ ? "events" : "requests") << " connection";
        ConsumeReadBuffer();  // frames that arrived with the close still count
        Fail();
        return;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      PLOG(ERROR) << "scroll ipc: read";
      Fail();
      return;
    }
    ConsumeReadBuffer();
  }

  void OnFdWritable(int fd) override {
    if (connecting_) {
      int so_error = 0;
      socklen_t len = sizeof(so_error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0) {
        so_error = errno;
      }
      if (so_error != 0) {
        LOG(ERROR) << "scroll ipc: connect(" << socket_path_
                   << "): " << strerror(so_error);
        Fail();
        return;
      }
      OnConnected();
      return;
    }
    FlushWrites();
  }

  void FlushWrites() {
    if (failed_ || !connected_) {
      return;
    }
    while (!write_buf_.empty()) {
      const ssize_t n = ::send(fd_.get(), write_buf_.data(), write_buf_.size(),
                               MSG_NOSIGNAL);
      if (n > 0) {
        write_buf_.erase(0, static_cast<size_t>(n));
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      }
      PLOG(ERROR) << "scroll ipc: send";
      Fail();
      return;
    }
    UpdateWatch();
  }

  void UpdateWatch() {
    if (failed_ || !fd_.is_valid()) {
      watch_.reset();
      return;
    }
    base::IOWatcher::FdWatchMode mode = base::IOWatcher::FdWatchMode::kRead;
    if (connecting_) {
      mode = base::IOWatcher::FdWatchMode::kWrite;
    } else if (!write_buf_.empty()) {
      mode = base::IOWatcher::FdWatchMode::kReadWrite;
    }
    if (watch_ && mode == watch_mode_) {
      return;
    }
    watch_.reset();
    base::IOWatcher* io_watcher = base::IOWatcher::Get();
    CHECK(io_watcher) << "scroll ipc: the IO thread has no IOWatcher";
    watch_ = io_watcher->WatchFileDescriptor(
        fd_.get(), base::IOWatcher::FdWatchDuration::kPersistent, mode, *this);
    watch_mode_ = mode;
    if (!watch_) {
      LOG(ERROR) << "scroll ipc: WatchFileDescriptor failed";
      Fail();
    }
  }

  void ConsumeReadBuffer() {
    while (!failed_ && read_buf_.size() >= kIpcHeaderLength) {
      const std::string_view header =
          std::string_view(read_buf_).substr(0, kIpcHeaderLength);
      if (header.substr(0, kIpcMagic.size()) != kIpcMagic) {
        LOG(ERROR) << "scroll ipc: bad magic, not an i3-ipc stream";
        Fail();
        return;
      }
      const uint32_t length = ReadU32(header.substr(6, 4));
      const uint32_t type = ReadU32(header.substr(10, 4));
      if (length > kMaxPayloadLength) {
        LOG(ERROR) << "scroll ipc: payload length " << length << " refused";
        Fail();
        return;
      }
      if (read_buf_.size() < kIpcHeaderLength + length) {
        return;  // split across reads: wait for the rest
      }
      std::string payload = read_buf_.substr(kIpcHeaderLength, length);
      read_buf_.erase(0, kIpcHeaderLength + length);
      core_->OnFrame(this, type, std::move(payload));
    }
  }

  void Fail() {
    if (failed_) {
      return;
    }
    failed_ = true;
    connecting_ = false;
    connected_ = false;
    watch_.reset();
    fd_.reset();
    core_->OnConnectionFailed(this);
  }

  const std::string socket_path_;
  const raw_ptr<Core> core_;
  const bool is_events_;
  base::ScopedFD fd_;
  // Declared after fd_, so it is destroyed first, on the IO thread.
  std::unique_ptr<base::IOWatcher::FdWatch> watch_;
  base::IOWatcher::FdWatchMode watch_mode_ =
      base::IOWatcher::FdWatchMode::kRead;
  bool connecting_ = false;
  bool connected_ = false;
  bool failed_ = false;
  std::string write_buf_;
  std::string read_buf_;
};

// ---------------------------------------------------------------------------
// Core, out of line.
// ---------------------------------------------------------------------------
ScrollIpcClient::Core::Core(
    std::string socket_path,
    std::vector<std::string> subscriptions,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    base::WeakPtr<ScrollIpcClient> client)
    : socket_path_(std::move(socket_path)),
      subscriptions_(std::move(subscriptions)),
      ui_task_runner_(std::move(ui_task_runner)),
      client_(std::move(client)) {}

ScrollIpcClient::Core::~Core() = default;

void ScrollIpcClient::Core::Init() {
  if (socket_path_.empty()) {
    socket_path_ = ResolveSocketPath();
  }
  VLOG(1) << "scroll ipc: connecting to " << socket_path_;
  events_ = std::make_unique<Connection>(socket_path_, this, /*is_events=*/true);
  requests_ =
      std::make_unique<Connection>(socket_path_, this, /*is_events=*/false);
  events_->Connect();
  if (!failed_) {
    requests_->Connect();
  }
}

void ScrollIpcClient::Core::OnConnectionReady(Connection* connection) {
  if (connection != events_.get()) {
    return;  // the requests connection just flushes what was queued
  }
  base::ListValue topics;
  for (const std::string& topic : subscriptions_) {
    topics.Append(topic);
  }
  events_->Send(kIpcSubscribe, base::WriteJson(topics).value_or("[]"));
}

void ScrollIpcClient::Core::OnConnectionFailed(Connection* connection) {
  Fail();
}

void ScrollIpcClient::Core::Fail() {
  if (failed_) {
    return;
  }
  failed_ = true;
  std::deque<ReplyCallback> pending;
  pending.swap(pending_);
  for (ReplyCallback& reply : pending) {
    PostReply(std::move(reply), std::nullopt);
  }
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ScrollIpcClient::DeliverDisconnected, client_));
}

void ScrollIpcClient::Core::OnFrame(Connection* connection,
                                    uint32_t type,
                                    std::string payload) {
  if (failed_) {
    return;
  }
  std::optional<base::Value> value;
  if (payload.empty()) {
    value = base::Value(base::DictValue());
  } else {
    value = base::JSONReader::Read(payload, base::JSON_PARSE_RFC);
    if (!value) {
      LOG(ERROR) << "scroll ipc: frame type " << type << " is not JSON";
    }
  }

  if (connection->is_events()) {
    if (!subscribed_) {
      const bool ok = type == kIpcSubscribe && value && value->is_dict() &&
                      value->GetDict().FindBool("success").value_or(false);
      if (!ok) {
        LOG(ERROR) << "scroll ipc: SUBSCRIBE was refused";
        Fail();
        return;
      }
      subscribed_ = true;
      ui_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&ScrollIpcClient::DeliverConnected, client_));
      return;
    }
    if (!(type & kIpcEventBit) || !value) {
      LOG(ERROR) << "scroll ipc: dropped frame type " << type
                 << " on the events connection";
      return;
    }
    ui_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&ScrollIpcClient::DeliverEvent, client_,
                                  type, std::move(*value)));
    return;
  }

  // The requests connection: replies come strictly in request order.
  if (pending_.empty()) {
    LOG(ERROR) << "scroll ipc: unsolicited reply of type " << type;
    return;
  }
  ReplyCallback reply = std::move(pending_.front());
  pending_.pop_front();
  PostReply(std::move(reply), std::move(value));
}

void ScrollIpcClient::Core::Request(uint32_t type,
                                    std::string payload,
                                    ReplyCallback reply) {
  if (failed_ || !requests_) {
    PostReply(std::move(reply), std::nullopt);
    return;
  }
  pending_.push_back(std::move(reply));
  requests_->Send(type, payload);
}

void ScrollIpcClient::Core::PostReply(ReplyCallback reply,
                                      std::optional<base::Value> value) {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ScrollIpcClient::DeliverReply, client_,
                                std::move(reply), std::move(value)));
}

// ---------------------------------------------------------------------------
// ScrollIpcClient
// ---------------------------------------------------------------------------

// static
std::string ScrollIpcClient::ResolveSocketPath() {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  return ResolveSocketPathFrom(*env, base::BindRepeating(&RunGetSocketPath));
}

// static
std::string ScrollIpcClient::ResolveSocketPathFrom(
    base::Environment& env,
    base::RepeatingCallback<std::string()> get_socketpath) {
  auto var = [&env](base::cstring_view name) {
    return env.GetVar(name).value_or(std::string());
  };
  if (std::string path = var("SCROLLSOCK"); !path.empty()) {
    return path;
  }
  if (std::string path = get_socketpath.Run(); !path.empty()) {
    return path;
  }
  for (base::cstring_view name :
       {base::cstring_view("SWAYSOCK"), base::cstring_view("I3SOCK")}) {
    if (std::string path = var(name); !path.empty()) {
      return path;
    }
  }
  const std::string runtime_dir = var("XDG_RUNTIME_DIR");
  if (runtime_dir.empty()) {
    return std::string();
  }
  for (const char* pattern : {"scroll-ipc.*.sock", "sway-ipc.*.sock"}) {
    std::string path = NewestMatch(base::FilePath(runtime_dir), pattern);
    if (!path.empty()) {
      return path;
    }
  }
  return std::string();
}

ScrollIpcClient::ScrollIpcClient(std::string socket_path,
                                 std::vector<std::string> subscriptions)
    : socket_path_(std::move(socket_path)),
      subscriptions_(std::move(subscriptions)),
      io_thread_("scroll_ipc_io") {}

ScrollIpcClient::~ScrollIpcClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  if (core_) {
    // The Connections' FdWatches must die on the IO thread that armed them.
    io_thread_.task_runner()->DeleteSoon(FROM_HERE, std::move(core_));
    io_thread_.Stop();
  }
}

void ScrollIpcClient::Start(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(observer);
  CHECK(!core_) << "ScrollIpcClient::Start() called twice";
  observer_ = observer;
  CHECK(io_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0)));
  core_ = std::make_unique<Core>(socket_path_, subscriptions_,
                                 base::SequencedTaskRunner::GetCurrentDefault(),
                                 weak_factory_.GetWeakPtr());
  io_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&Core::Init, base::Unretained(core_.get())));
}

void ScrollIpcClient::Request(uint32_t type,
                              std::string payload,
                              ReplyCallback reply) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!core_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(reply), std::nullopt));
    return;
  }
  // core_ is deleted on the IO thread only after this object is gone, and
  // tasks on the IO thread run in order, so Unretained is safe here.
  io_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&Core::Request, base::Unretained(core_.get()), type,
                     std::move(payload), std::move(reply)));
}

void ScrollIpcClient::DeliverConnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!disconnected_) {
    observer_->OnConnected();
  }
}

void ScrollIpcClient::DeliverEvent(uint32_t type, base::Value payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!disconnected_) {
    observer_->OnEvent(type, std::move(payload));
  }
}

void ScrollIpcClient::DeliverDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (disconnected_) {
    return;
  }
  disconnected_ = true;
  observer_->OnDisconnected();
}

void ScrollIpcClient::DeliverReply(ReplyCallback reply,
                                   std::optional<base::Value> value) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(reply).Run(std::move(value));
}

}  // namespace views_shell::scroll
