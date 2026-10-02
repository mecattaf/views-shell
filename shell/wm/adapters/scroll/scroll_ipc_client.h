// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The i3-ipc transport under the scroll (and sway) adapter. Two AF_UNIX
// connections to the compositor socket: one subscribed to events, one for
// requests, so a slow reply never blocks the event stream
// (docs/compositor-adapters.md).
//
// Framing is i3-ipc: the 6-byte magic `i3-ipc`, a uint32 payload length and a
// uint32 message type in native byte order, then the payload (raw command text
// for RUN_COMMAND and SEND_TICK, JSON or empty otherwise; every reply and
// every event is JSON). The decoder tolerates frames split across reads and
// several frames in one read.
//
// Threading: every socket operation and every base::JSONReader parse runs on
// the client's own IO thread (MessagePumpType::IO, base::IOWatcher). Every
// Observer call and every reply runs on the sequence that called Start(),
// bound to a WeakPtr of the client, so nothing is delivered after the client
// is destroyed.
//
// One client is one connection attempt. It never reconnects: after
// OnDisconnected() it is dead, and the adapter above makes a new one (with its
// own backoff and a full resync).

#ifndef VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_
#define VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"

namespace base {
class Environment;
}

namespace views_shell::scroll {

// Message and event types as numbered by scroll's include/ipc.h (the i3/sway
// numbering plus scroll's own; scroll is MIT, read, not copied).
enum IpcType : uint32_t {
  kIpcRunCommand = 0,
  kIpcGetWorkspaces = 1,
  kIpcSubscribe = 2,
  kIpcGetOutputs = 3,
  kIpcGetTree = 4,
  kIpcGetVersion = 7,
  kIpcGetConfig = 9,
  kIpcSendTick = 10,
  kIpcGetScroller = 120,  // scroll only
  kIpcGetTrails = 121,    // scroll only
  kIpcGetSpaces = 122,    // scroll only
  kIpcGetBindings = 123,  // scroll only
  // 124 (LUA_EVAL) is deliberately absent from the core client.
};

// Events are frames with the high bit set.
inline constexpr uint32_t kIpcEventBit = 0x80000000u;
inline constexpr uint32_t kIpcWorkspaceEvent = kIpcEventBit | 0;
inline constexpr uint32_t kIpcOutputEvent = kIpcEventBit | 1;
inline constexpr uint32_t kIpcModeEvent = kIpcEventBit | 2;
inline constexpr uint32_t kIpcWindowEvent = kIpcEventBit | 3;
inline constexpr uint32_t kIpcBindingEvent = kIpcEventBit | 5;
inline constexpr uint32_t kIpcShutdownEvent = kIpcEventBit | 6;
inline constexpr uint32_t kIpcTickEvent = kIpcEventBit | 7;

// The 14-byte header: magic, length, type.
inline constexpr size_t kIpcHeaderLength = 14;

// One framed message, header included, as it goes on the wire.
std::string BuildIpcFrame(uint32_t type, std::string_view payload);

class ScrollIpcClient {
 public:
  // A reply, on the Start() sequence: the parsed JSON (an empty payload parses
  // as an empty dict), or nullopt when the connection failed first or the
  // payload was not JSON.
  using ReplyCallback = base::OnceCallback<void(std::optional<base::Value>)>;

  class Observer {
   public:
    virtual ~Observer() = default;
    // Both connections are up and SUBSCRIBE was acknowledged. Events can
    // arrive from now on (sway sends a `tick` with first:true right away).
    virtual void OnConnected() = 0;
    // One event frame, in the order the compositor sent it.
    virtual void OnEvent(uint32_t type, base::Value payload) = 0;
    // The connection attempt failed, or a connection closed. Called once;
    // every unanswered request has been answered with nullopt before it.
    virtual void OnDisconnected() = 0;
  };

  // The socket, as the adapter resolves it: $SCROLLSOCK, then the output of
  // `scroll --get-socketpath`, then $SWAYSOCK, then $I3SOCK, then the newest
  // scroll-ipc.*.sock and then sway-ipc.*.sock in $XDG_RUNTIME_DIR. Empty when
  // nothing is found. Blocking (it may run a process); the client calls it on
  // its IO thread when constructed with an empty path.
  static std::string ResolveSocketPath();
  // The same order over an explicit environment and `scroll --get-socketpath`
  // runner, for tests.
  static std::string ResolveSocketPathFrom(
      base::Environment& env,
      base::RepeatingCallback<std::string()> get_socketpath);

  // `socket_path` empty means ResolveSocketPath() on the IO thread.
  // `subscriptions` are the event names sent in SUBSCRIBE.
  ScrollIpcClient(std::string socket_path,
                  std::vector<std::string> subscriptions);
  ScrollIpcClient(const ScrollIpcClient&) = delete;
  ScrollIpcClient& operator=(const ScrollIpcClient&) = delete;
  // Closes both connections and joins the IO thread. No callback runs after.
  ~ScrollIpcClient();

  // Starts the IO thread and connects both sockets. Call once.
  void Start(Observer* observer);

  // Sends one request on the requests connection. Requests may be sent before
  // OnConnected (they wait for the socket) and are answered strictly in
  // order. Before Start() or after a failure, `reply` runs with nullopt
  // (posted, never synchronously).
  void Request(uint32_t type, std::string payload, ReplyCallback reply);

 private:
  class Core;        // IO-thread state: both connections and the reply FIFO
  class Connection;  // one framed non-blocking socket and its FdWatch

  // Delivery from the Core, on the Start() sequence.
  void DeliverConnected();
  void DeliverEvent(uint32_t type, base::Value payload);
  void DeliverDisconnected();
  void DeliverReply(ReplyCallback reply, std::optional<base::Value> value);

  SEQUENCE_CHECKER(sequence_checker_);
  std::string socket_path_;
  std::vector<std::string> subscriptions_;
  base::Thread io_thread_;
  std::unique_ptr<Core> core_;  // created here, destroyed on the IO thread
  raw_ptr<Observer> observer_ = nullptr;
  bool disconnected_ = false;
  base::WeakPtrFactory<ScrollIpcClient> weak_factory_{this};
};

}  // namespace views_shell::scroll

#endif  // VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_
