// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/signal/niri/niri_ipc_client.h
//
// NiriIpcClient -- the SignalSource control seam. It owns the single
// $NIRI_SOCKET AF_UNIX connection to the niri compositor, performs the
// EventStream handshake, decodes the NDJSON event stream into typed
// agency::niri::Event values (niri_events.h), and dispatches each to the
// workspace/window/output producer callbacks the shell binds.
//
// Threading: the client lives entirely on ONE sequence -- the IO thread it is
// constructed on (BrowserThread::IO in the shell; any CurrentIOThread in a
// test). It never blocks: the connect() is non-blocking, and readability is
// driven by base::MessagePumpForIO's fd watcher
// (base::MessagePumpForIO::FdWatcher, armed via
// base::CurrentIOThread::Get()->WatchFileDescriptor). There is NO reader
// thread and NO busy loop. All callbacks fire on this same sequence, so the
// producers above need no locking.
//
// Resilience: niri restarts (config crash, compositor upgrade) close the
// socket. On EOF or any read/connect error the client tears the watch down and
// schedules a reconnect with exponential backoff + jitter (250ms base, doubling
// to a cap), re-emitting the full state via the fresh WorkspacesChanged/
// WindowsChanged resync niri sends on every new EventStream. This is why the
// consumer treats those two events as authoritative full vectors.

#ifndef AGENCY_SIGNAL_NIRI_NIRI_IPC_CLIENT_H_
#define AGENCY_SIGNAL_NIRI_NIRI_IPC_CLIENT_H_

#include <memory>
#include <string>
#include <string_view>

#include "agency/signal/niri/niri_events.h"
#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace agency {
namespace niri {

// The callback vocabulary the shell binds. One RepeatingCallback per fanned-out
// producer domain; the client invokes exactly the callbacks whose events arrive.
// Any callback may be null (a consumer that does not care about, say, casts
// simply leaves that slot unbound) -- the dispatcher null-checks before firing.
struct NiriEventCallbacks {
  // Full workspace-vector resync (also fired first on every (re)connect).
  base::RepeatingCallback<void(const WorkspacesChanged&)> on_workspaces_changed;
  // Full window-vector resync (also fired on every (re)connect).
  base::RepeatingCallback<void(const WindowsChanged&)> on_windows_changed;
  base::RepeatingCallback<void(const WindowOpenedOrChanged&)>
      on_window_opened_or_changed;
  base::RepeatingCallback<void(const WindowClosed&)> on_window_closed;
  base::RepeatingCallback<void(const WorkspaceActivated&)>
      on_workspace_activated;
  base::RepeatingCallback<void(const WindowFocusChanged&)>
      on_window_focus_changed;
  base::RepeatingCallback<void(const WorkspaceUrgencyChanged&)>
      on_workspace_urgency_changed;
  base::RepeatingCallback<void(const KeyboardLayoutsChanged&)>
      on_keyboard_layouts_changed;
  base::RepeatingCallback<void(const OverviewOpenedOrClosed&)>
      on_overview_opened_or_closed;
  base::RepeatingCallback<void(const ConfigLoaded&)> on_config_loaded;
  base::RepeatingCallback<void(const CastsChanged&)> on_casts_changed;

  NiriEventCallbacks();
  NiriEventCallbacks(const NiriEventCallbacks&);
  NiriEventCallbacks& operator=(const NiriEventCallbacks&);
  ~NiriEventCallbacks();
};

class NiriIpcClient : public base::MessagePumpForIO::FdWatcher {
 public:
  // `socket_path` is $NIRI_SOCKET. `callbacks` is moved in and held for the
  // client's lifetime. Construction does NOT connect; call Start().
  NiriIpcClient(std::string socket_path, NiriEventCallbacks callbacks);

  NiriIpcClient(const NiriIpcClient&) = delete;
  NiriIpcClient& operator=(const NiriIpcClient&) = delete;

  ~NiriIpcClient() override;

  // Begin connecting. Idempotent-safe to call once; a second call while already
  // connected/connecting is a no-op. On failure the backoff loop takes over --
  // Start() never blocks and never fails synchronously in a way the caller must
  // handle; the socket simply comes up when niri is reachable.
  void Start();

  // True between a successful handshake and the next disconnect. Exposed for the
  // shell's health surface / tests.
  bool is_connected() const;

  // The delay the next reconnect attempt will wait (for test observation).
  base::TimeDelta next_backoff_for_testing() const { return backoff_; }

 private:
  // base::MessagePumpForIO::FdWatcher.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  // Open the AF_UNIX socket non-blocking and connect to socket_path_. Returns
  // true if the fd is open and either connected or connecting; false (and the
  // fd closed) on a hard socket()/connect() error, which routes to backoff.
  bool OpenAndConnect();

  // Called once the socket is writable-or-connected: sends the `"EventStream"\n`
  // request and arms the read watch. Split from OpenAndConnect so an in-progress
  // (EINPROGRESS) connect completes via OnFileCanWriteWithoutBlocking first.
  void SendEventStreamRequest();

  // Arm/disarm the level-triggered readability watch on socket_fd_.
  void StartWatchingReadable();
  void StopWatching();

  // Drain everything currently readable into read_buffer_, then peel off every
  // complete '\n'-terminated line and route it through HandleLine. Returns false
  // if the peer closed (EOF) or a fatal read error occurred -> caller reconnects.
  bool ReadAndDispatchAvailable();

  // Route one complete NDJSON line. The first line after connect is the
  // `{"Ok":...}` handshake (HandleHandshakeLine); every subsequent line is a
  // single-key event object (HandleEventLine).
  void HandleLine(std::string_view line);
  void HandleHandshakeLine(std::string_view line);
  void HandleEventLine(std::string_view line);

  // Tear down the current connection and schedule a reconnect with the current
  // backoff (then grow it toward the cap). `reason` is logged.
  void ScheduleReconnect(std::string_view reason);
  void Reconnect();
  void ResetBackoff();

  SEQUENCE_CHECKER(sequence_checker_);

  const std::string socket_path_;
  const NiriEventCallbacks callbacks_;

  base::ScopedFD socket_fd_;
  base::MessagePumpForIO::FdWatchController read_watch_;
  base::MessagePumpForIO::FdWatchController write_watch_;

  // Byte accumulator for partial NDJSON lines across reads. niri can split an
  // event object across two recv() boundaries, so a line is only dispatched
  // once its terminating '\n' has arrived.
  std::string read_buffer_;

  // False until the `{"Ok":...}` handshake line has been consumed on the
  // current connection; the first post-connect line is always the handshake.
  bool handshake_done_ = false;
  bool connected_ = false;

  // Exponential-backoff state. Base 250ms, doubled per failed attempt, capped;
  // ScheduleReconnect applies +/- jitter around `backoff_` so a niri restart
  // does not thundering-herd every client at the same instant.
  base::TimeDelta backoff_;

  base::WeakPtrFactory<NiriIpcClient> weak_factory_{this};
};

}  // namespace niri
}  // namespace agency

#endif  // AGENCY_SIGNAL_NIRI_NIRI_IPC_CLIENT_H_
