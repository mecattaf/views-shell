// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/signal/niri/niri_ipc_client.cc

#include "agency/signal/niri/niri_ipc_client.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/task/current_thread.h"
#include "base/posix/eintr_wrapper.h"
#include "base/rand_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"

namespace agency {
namespace niri {

namespace {

// The one request that puts the socket into streaming mode. niri's IPC accepts a
// JSON request per connection; the bare JSON string "EventStream" (newline
// terminated) is the one this client speaks. Captured live in the Wave-2 spike.
constexpr char kEventStreamRequest[] = "\"EventStream\"\n";

// Backoff schedule. 250ms base, doubling to a 30s cap; a full niri restart is
// seconds, and 30s bounds the reconnect storm if niri is gone for good.
constexpr base::TimeDelta kBackoffBase = base::Milliseconds(250);
constexpr base::TimeDelta kBackoffCap = base::Seconds(30);
// Jitter is +/- this fraction of the current backoff.
constexpr double kJitterFraction = 0.25;

// Cap on the in-flight line accumulator. A single niri event object is well
// under a few KiB; anything past this means a desync/garbage peer and we drop
// the connection rather than grow unbounded.
constexpr size_t kMaxLineBufferBytes = 1 << 20;  // 1 MiB

// ---- decode helpers over base::Value ----------------------------------------
// Each returns std::optional and does its own type-checking; a wire field of the
// wrong type (a niri version drift) yields nullopt rather than a crash, and the
// caller decides whether that field is required.

std::optional<uint64_t> AsU64(const base::Value* v) {
  if (!v) {
    return std::nullopt;
  }
  // JSON integers arrive as int when they fit, else as double. niri object ids
  // are u64 and can exceed int32, so accept both representations.
  if (v->is_int()) {
    const int i = v->GetInt();
    return i < 0 ? std::nullopt : std::optional<uint64_t>(static_cast<uint64_t>(i));
  }
  if (v->is_double()) {
    const double d = v->GetDouble();
    return d < 0 ? std::nullopt
                 : std::optional<uint64_t>(static_cast<uint64_t>(d));
  }
  return std::nullopt;
}

std::optional<int32_t> AsI32(const base::Value* v) {
  if (!v) {
    return std::nullopt;
  }
  if (v->is_int()) {
    return v->GetInt();
  }
  if (v->is_double()) {
    return static_cast<int32_t>(v->GetDouble());
  }
  return std::nullopt;
}

std::optional<double> AsDouble(const base::Value* v) {
  if (!v) {
    return std::nullopt;
  }
  if (v->is_double()) {
    return v->GetDouble();
  }
  if (v->is_int()) {
    return static_cast<double>(v->GetInt());
  }
  return std::nullopt;
}

std::optional<std::string> AsString(const base::Value* v) {
  if (v && v->is_string()) {
    return v->GetString();
  }
  return std::nullopt;
}

bool AsBool(const base::Value* v, bool fallback) {
  return (v && v->is_bool()) ? v->GetBool() : fallback;
}

// Decode a JSON array of exactly two numbers into a pair, applying `conv` to
// each. Returns nullopt if the value is not a 2-element numeric array.
template <typename T>
std::optional<std::pair<T, T>> AsNumberPair(
    const base::Value* v,
    std::optional<T> (*conv)(const base::Value*)) {
  if (!v || !v->is_list()) {
    return std::nullopt;
  }
  const base::ListValue& list = v->GetList();
  if (list.size() != 2) {
    return std::nullopt;
  }
  std::optional<T> a = conv(&list[0]);
  std::optional<T> b = conv(&list[1]);
  if (!a || !b) {
    return std::nullopt;
  }
  return std::make_pair(*a, *b);
}

Workspace DecodeWorkspace(const base::DictValue& d) {
  Workspace ws;
  ws.id = AsU64(d.Find("id")).value_or(0);
  ws.idx = static_cast<uint8_t>(AsI32(d.Find("idx")).value_or(0));
  ws.name = AsString(d.Find("name"));
  ws.output = AsString(d.Find("output"));
  ws.is_urgent = AsBool(d.Find("is_urgent"), false);
  ws.is_active = AsBool(d.Find("is_active"), false);
  ws.is_focused = AsBool(d.Find("is_focused"), false);
  ws.active_window_id = AsU64(d.Find("active_window_id"));
  return ws;
}

WindowLayout DecodeLayout(const base::DictValue& d) {
  WindowLayout layout;
  layout.pos_in_scrolling_layout =
      AsNumberPair<int32_t>(d.Find("pos_in_scrolling_layout"), &AsI32);
  layout.tile_size = AsNumberPair<double>(d.Find("tile_size"), &AsDouble);
  layout.window_size =
      AsNumberPair<int32_t>(d.Find("window_size"), &AsI32);
  layout.tile_pos_in_workspace_view =
      AsNumberPair<double>(d.Find("tile_pos_in_workspace_view"), &AsDouble);
  layout.window_offset_in_tile =
      AsNumberPair<double>(d.Find("window_offset_in_tile"), &AsDouble);
  return layout;
}

Window DecodeWindow(const base::DictValue& d) {
  Window w;
  w.id = AsU64(d.Find("id")).value_or(0);
  w.title = AsString(d.Find("title"));
  w.app_id = AsString(d.Find("app_id"));
  w.pid = AsI32(d.Find("pid"));
  w.workspace_id = AsU64(d.Find("workspace_id"));
  w.is_focused = AsBool(d.Find("is_focused"), false);
  w.is_floating = AsBool(d.Find("is_floating"), false);
  w.is_urgent = AsBool(d.Find("is_urgent"), false);
  if (const base::Value* layout = d.Find("layout"); layout && layout->is_dict()) {
    w.layout = DecodeLayout(layout->GetDict());
  }
  // focus_timestamp is a nested {secs, nanos} object in niri's schema.
  if (const base::Value* ts = d.Find("focus_timestamp");
      ts && ts->is_dict()) {
    const base::DictValue& tsd = ts->GetDict();
    w.focus_timestamp_secs = AsU64(tsd.Find("secs"));
    w.focus_timestamp_nanos = AsU64(tsd.Find("nanos"));
  }
  return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// NiriEventCallbacks
// ---------------------------------------------------------------------------

NiriEventCallbacks::NiriEventCallbacks() = default;
NiriEventCallbacks::NiriEventCallbacks(const NiriEventCallbacks&) = default;
NiriEventCallbacks& NiriEventCallbacks::operator=(const NiriEventCallbacks&) =
    default;
NiriEventCallbacks::~NiriEventCallbacks() = default;

// ---------------------------------------------------------------------------
// NiriIpcClient
// ---------------------------------------------------------------------------

NiriIpcClient::NiriIpcClient(std::string socket_path,
                             NiriEventCallbacks callbacks)
    : socket_path_(std::move(socket_path)),
      callbacks_(std::move(callbacks)),
      read_watch_(FROM_HERE),
      write_watch_(FROM_HERE),
      backoff_(kBackoffBase) {
  // The client's whole life is on the sequence it is built on; that sequence
  // must be an IO thread because the fd watcher is a MessagePumpForIO facility.
  CHECK(base::CurrentIOThread::IsSet())
      << "agency::niri::NiriIpcClient must be constructed on an IO thread "
         "(needs base::MessagePumpForIO for the socket fd watch)";
}

NiriIpcClient::~NiriIpcClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopWatching();
}

void NiriIpcClient::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (socket_fd_.is_valid()) {
    // Already connected or mid-connect; Start() is idempotent.
    return;
  }
  if (!OpenAndConnect()) {
    ScheduleReconnect("initial connect failed");
  }
}

bool NiriIpcClient::is_connected() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return connected_;
}

bool NiriIpcClient::OpenAndConnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!socket_fd_.is_valid());

  handshake_done_ = false;
  connected_ = false;
  read_buffer_.clear();

  base::ScopedFD fd(
      socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "agency::niri: socket(AF_UNIX) failed";
    return false;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  // sun_path is fixed-size; a $NIRI_SOCKET longer than it cannot be represented
  // and is a hard configuration error, not a transient one.
  if (socket_path_.size() >= sizeof(addr.sun_path)) {
    LOG(ERROR) << "agency::niri: NIRI_SOCKET path too long ("
               << socket_path_.size() << " >= " << sizeof(addr.sun_path)
               << "): " << socket_path_;
    return false;
  }
  // span::copy_from bounds the write into the fixed sun_path buffer instead of
  // a raw memcpy (-Wunsafe-buffer-usage-in-libc-call); the size check above
  // guarantees the trailing NUL from the memset() survives.
  base::span(addr.sun_path).first(socket_path_.size()).copy_from(
      base::span(socket_path_));

  const int rv = HANDLE_EINTR(
      connect(fd.get(), reinterpret_cast<struct sockaddr*>(&addr),
              sizeof(addr)));
  if (rv == 0) {
    // Connected synchronously (the common case for a local, already-listening
    // AF_UNIX socket). Move the fd in and send the request straight away.
    socket_fd_ = std::move(fd);
    SendEventStreamRequest();
    return true;
  }
  if (errno == EINPROGRESS) {
    // Connect is in flight; completion is signalled by writability. Watch for
    // writable, then SendEventStreamRequest() from OnFileCanWriteWithoutBlocking.
    socket_fd_ = std::move(fd);
    const bool ok = base::CurrentIOThread::Get()->WatchFileDescriptor(
        socket_fd_.get(), /*persistent=*/false,
        base::MessagePumpForIO::WATCH_WRITE, &write_watch_, this);
    if (!ok) {
      LOG(ERROR) << "agency::niri: WatchFileDescriptor(WATCH_WRITE) failed";
      socket_fd_.reset();
      return false;
    }
    return true;
  }
  PLOG(ERROR) << "agency::niri: connect(" << socket_path_ << ") failed";
  return false;
}

void NiriIpcClient::OnFileCanWriteWithoutBlocking(int fd) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(fd, socket_fd_.get());
  // The in-progress connect just resolved. SO_ERROR tells us whether it
  // succeeded; a nonzero value means the connect failed and we reconnect.
  write_watch_.StopWatchingFileDescriptor();
  int so_error = 0;
  socklen_t len = sizeof(so_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 ||
      so_error != 0) {
    ScheduleReconnect("async connect failed");
    return;
  }
  SendEventStreamRequest();
}

void NiriIpcClient::SendEventStreamRequest() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(socket_fd_.is_valid());
  // The request is a dozen bytes into an empty local socket buffer -- a single
  // write() completes it in practice. Loop over partial writes anyway so a
  // pathologically small buffer can't truncate the request; a genuine WOULDBLOCK
  // on the very first bytes is treated as a connection fault (reconnect) rather
  // than adding a write-watch state machine for a 14-byte payload.
  // std::string_view advances via remove_prefix (bounds-checked) instead of raw
  // `data += n` pointer arithmetic (-Wunsafe-buffer-usage). The view stops at
  // the NUL, so only the request bytes (not the terminator) go on the wire.
  std::string_view remaining = kEventStreamRequest;
  while (!remaining.empty()) {
    const ssize_t n = HANDLE_EINTR(
        write(socket_fd_.get(), remaining.data(), remaining.size()));
    if (n > 0) {
      remaining.remove_prefix(static_cast<size_t>(n));
      continue;
    }
    ScheduleReconnect("failed to write EventStream request");
    return;
  }
  // Request is on the wire; the next thing to arrive is the {"Ok":...}
  // handshake line, then the NDJSON event stream. Arm the read watch.
  StartWatchingReadable();
}

void NiriIpcClient::StartWatchingReadable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(socket_fd_.is_valid());
  const bool ok = base::CurrentIOThread::Get()->WatchFileDescriptor(
      socket_fd_.get(), /*persistent=*/true,
      base::MessagePumpForIO::WATCH_READ, &read_watch_, this);
  if (!ok) {
    LOG(ERROR) << "agency::niri: WatchFileDescriptor(WATCH_READ) failed";
    ScheduleReconnect("failed to arm read watch");
  }
}

void NiriIpcClient::StopWatching() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  read_watch_.StopWatchingFileDescriptor();
  write_watch_.StopWatchingFileDescriptor();
  socket_fd_.reset();
  connected_ = false;
  handshake_done_ = false;
  read_buffer_.clear();
}

void NiriIpcClient::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(fd, socket_fd_.get());
  if (!ReadAndDispatchAvailable()) {
    ScheduleReconnect("socket EOF or read error");
  }
}

bool NiriIpcClient::ReadAndDispatchAvailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Drain the socket until it would block. The watch is level-triggered/
  // persistent, but reading to EAGAIN here keeps the buffer from lagging behind
  // a burst of events.
  char chunk[4096];
  for (;;) {
    const ssize_t n = HANDLE_EINTR(read(socket_fd_.get(), chunk, sizeof(chunk)));
    if (n > 0) {
      read_buffer_.append(chunk, static_cast<size_t>(n));
      if (read_buffer_.size() > kMaxLineBufferBytes) {
        LOG(ERROR) << "agency::niri: line buffer exceeded "
                   << kMaxLineBufferBytes << " bytes without a newline; "
                   << "dropping connection";
        return false;
      }
      continue;
    }
    if (n == 0) {
      // Orderly EOF: niri closed the stream (restart/shutdown).
      return false;
    }
    // n < 0.
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;  // Nothing more to read right now.
    }
    if (errno == EINTR) {
      continue;  // HANDLE_EINTR should have caught this, but be defensive.
    }
    PLOG(ERROR) << "agency::niri: read() failed";
    return false;
  }

  // Peel off every complete '\n'-terminated line. A trailing partial line stays
  // in read_buffer_ for the next readable edge.
  size_t start = 0;
  for (;;) {
    const size_t nl = read_buffer_.find('\n', start);
    if (nl == std::string::npos) {
      break;
    }
    HandleLine(std::string_view(read_buffer_).substr(start, nl - start));
    start = nl + 1;
    // HandleLine -> a callback could in principle re-enter and StopWatching();
    // guard against then touching a reset buffer.
    if (!socket_fd_.is_valid()) {
      return true;
    }
  }
  if (start > 0) {
    read_buffer_.erase(0, start);
  }
  return true;
}

void NiriIpcClient::HandleLine(std::string_view line) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Skip blank lines (defensive; niri does not emit them).
  if (line.empty()) {
    return;
  }
  if (!handshake_done_) {
    HandleHandshakeLine(line);
    return;
  }
  HandleEventLine(line);
}

void NiriIpcClient::HandleHandshakeLine(std::string_view line) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The handshake is `{"Ok": ...}` on success or `{"Err": ...}` on a rejected
  // request. Parse and require the "Ok" key; anything else is a protocol
  // failure and we reconnect (a niri that rejects "EventStream" is unusable).
  std::optional<base::Value> parsed =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict() || !parsed->GetDict().Find("Ok")) {
    LOG(ERROR) << "agency::niri: EventStream handshake was not {\"Ok\":...}: "
               << line;
    ScheduleReconnect("bad handshake");
    return;
  }
  handshake_done_ = true;
  connected_ = true;
  // A completed handshake is a proven-good connection: reset backoff so the next
  // disconnect starts from the fast base delay again.
  ResetBackoff();
  VLOG(1) << "agency::niri: EventStream connected";
}

void NiriIpcClient::HandleEventLine(std::string_view line) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    LOG(WARNING) << "agency::niri: event line was not a JSON object: " << line;
    return;
  }
  const base::DictValue& obj = parsed->GetDict();
  // Every event is a single-key tagged object; take the one and only key.
  if (obj.size() != 1) {
    LOG(WARNING) << "agency::niri: event object was not single-key (size="
                 << obj.size() << "): " << line;
    return;
  }
  const auto it = obj.begin();
  const std::string& wire_key = it->first;
  const base::Value& payload = it->second;
  const EventTag tag = WireKeyToEventTag(wire_key);

  // Dispatch. Each arm decodes the payload dict into its typed struct and fires
  // the bound callback (if any). An unbound callback for a subscribed event is
  // fine -- the consumer simply doesn't care about that domain.
  switch (tag) {
    case EventTag::kWorkspacesChanged: {
      if (!payload.is_dict() || !callbacks_.on_workspaces_changed) {
        return;
      }
      const base::Value* arr = payload.GetDict().Find("workspaces");
      if (!arr || !arr->is_list()) {
        return;
      }
      WorkspacesChanged ev;
      ev.workspaces.reserve(arr->GetList().size());
      for (const base::Value& item : arr->GetList()) {
        if (item.is_dict()) {
          ev.workspaces.push_back(DecodeWorkspace(item.GetDict()));
        }
      }
      callbacks_.on_workspaces_changed.Run(ev);
      return;
    }
    case EventTag::kWindowsChanged: {
      if (!payload.is_dict() || !callbacks_.on_windows_changed) {
        return;
      }
      const base::Value* arr = payload.GetDict().Find("windows");
      if (!arr || !arr->is_list()) {
        return;
      }
      WindowsChanged ev;
      ev.windows.reserve(arr->GetList().size());
      for (const base::Value& item : arr->GetList()) {
        if (item.is_dict()) {
          ev.windows.push_back(DecodeWindow(item.GetDict()));
        }
      }
      callbacks_.on_windows_changed.Run(ev);
      return;
    }
    case EventTag::kWindowOpenedOrChanged: {
      if (!payload.is_dict() || !callbacks_.on_window_opened_or_changed) {
        return;
      }
      const base::Value* win = payload.GetDict().Find("window");
      if (!win || !win->is_dict()) {
        return;
      }
      WindowOpenedOrChanged ev;
      ev.window = DecodeWindow(win->GetDict());
      callbacks_.on_window_opened_or_changed.Run(ev);
      return;
    }
    case EventTag::kWindowClosed: {
      if (!payload.is_dict() || !callbacks_.on_window_closed) {
        return;
      }
      WindowClosed ev;
      ev.id = AsU64(payload.GetDict().Find("id")).value_or(0);
      callbacks_.on_window_closed.Run(ev);
      return;
    }
    case EventTag::kWorkspaceActivated: {
      if (!payload.is_dict() || !callbacks_.on_workspace_activated) {
        return;
      }
      const base::DictValue& d = payload.GetDict();
      WorkspaceActivated ev;
      ev.id = AsU64(d.Find("id")).value_or(0);
      ev.focused = AsBool(d.Find("focused"), false);
      callbacks_.on_workspace_activated.Run(ev);
      return;
    }
    case EventTag::kWindowFocusChanged: {
      if (!payload.is_dict() || !callbacks_.on_window_focus_changed) {
        return;
      }
      WindowFocusChanged ev;
      // `id` is null when focus left all windows.
      ev.id = AsU64(payload.GetDict().Find("id"));
      callbacks_.on_window_focus_changed.Run(ev);
      return;
    }
    case EventTag::kWorkspaceUrgencyChanged: {
      if (!payload.is_dict() || !callbacks_.on_workspace_urgency_changed) {
        return;
      }
      const base::DictValue& d = payload.GetDict();
      WorkspaceUrgencyChanged ev;
      ev.id = AsU64(d.Find("id")).value_or(0);
      ev.urgent = AsBool(d.Find("urgent"), false);
      callbacks_.on_workspace_urgency_changed.Run(ev);
      return;
    }
    case EventTag::kKeyboardLayoutsChanged: {
      if (!payload.is_dict() || !callbacks_.on_keyboard_layouts_changed) {
        return;
      }
      // Payload nests a `keyboard_layouts: {names:[...], current_idx:N}`.
      const base::Value* kl = payload.GetDict().Find("keyboard_layouts");
      if (!kl || !kl->is_dict()) {
        return;
      }
      const base::DictValue& kld = kl->GetDict();
      KeyboardLayoutsChanged ev;
      if (const base::Value* names = kld.Find("names");
          names && names->is_list()) {
        for (const base::Value& n : names->GetList()) {
          if (n.is_string()) {
            ev.names.push_back(n.GetString());
          }
        }
      }
      ev.current_idx =
          static_cast<uint8_t>(AsI32(kld.Find("current_idx")).value_or(0));
      callbacks_.on_keyboard_layouts_changed.Run(ev);
      return;
    }
    case EventTag::kOverviewOpenedOrClosed: {
      if (!payload.is_dict() || !callbacks_.on_overview_opened_or_closed) {
        return;
      }
      OverviewOpenedOrClosed ev;
      ev.is_open = AsBool(payload.GetDict().Find("is_open"), false);
      callbacks_.on_overview_opened_or_closed.Run(ev);
      return;
    }
    case EventTag::kConfigLoaded: {
      if (!payload.is_dict() || !callbacks_.on_config_loaded) {
        return;
      }
      ConfigLoaded ev;
      ev.failed = AsBool(payload.GetDict().Find("failed"), false);
      callbacks_.on_config_loaded.Run(ev);
      return;
    }
    case EventTag::kCastsChanged: {
      if (!payload.is_dict() || !callbacks_.on_casts_changed) {
        return;
      }
      CastsChanged ev;
      if (const base::Value* casts = payload.GetDict().Find("casts");
          casts && casts->is_list()) {
        ev.active_cast_count = casts->GetList().size();
      }
      callbacks_.on_casts_changed.Run(ev);
      return;
    }
    case EventTag::kUnknown:
      // A niri newer than this parser emitted an event we don't model. Log once
      // at low verbosity and keep the stream alive -- forward compatibility, not
      // a fault.
      VLOG(1) << "agency::niri: ignoring unknown event tag '" << wire_key << "'";
      return;
  }
}

void NiriIpcClient::ScheduleReconnect(std::string_view reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(WARNING) << "agency::niri: reconnecting (" << reason << ") in "
               << backoff_.InMillisecondsF() << "ms";
  StopWatching();

  // Apply symmetric jitter around the current backoff so a compositor restart
  // does not resynchronize every client onto the same reconnect instant.
  const double jitter =
      base::RandDouble() * 2.0 * kJitterFraction - kJitterFraction;
  base::TimeDelta delay = backoff_ + backoff_ * jitter;
  if (delay < base::TimeDelta()) {
    delay = base::TimeDelta();
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&NiriIpcClient::Reconnect, weak_factory_.GetWeakPtr()),
      delay);

  // Grow the backoff toward the cap for the NEXT failure. A successful
  // handshake resets it (ResetBackoff in HandleHandshakeLine).
  backoff_ = std::min(backoff_ * 2, kBackoffCap);
}

void NiriIpcClient::Reconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!socket_fd_.is_valid());
  if (!OpenAndConnect()) {
    ScheduleReconnect("reconnect attempt failed");
  }
}

void NiriIpcClient::ResetBackoff() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  backoff_ = kBackoffBase;
}

}  // namespace niri
}  // namespace agency
