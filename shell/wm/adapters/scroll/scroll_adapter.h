// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The scroll and sway CompositorAdapter, over i3-ipc (ScrollIpcClient).
//
// Connect: SUBSCRIBE ["workspace","window","output","binding","shutdown",
// "tick"] on the events connection first, then GET_VERSION (names the variant
// and probes capabilities), then one refresh: GET_OUTPUTS, GET_WORKSPACES and
// GET_TREE, pipelined, folded into one WmSnapshot. Every workspace, window or
// output event asks for a refresh; at most one refresh is in flight, and
// events that arrive meanwhile mark the state dirty, which sends one more when
// the current one lands.
//
// Commands: typed WmCommands become RUN_COMMAND strings here and nowhere
// else. After a successful reply the adapter sends SEND_TICK with a unique
// payload. The matching `tick` event comes after every event the command
// caused, and the command is done once a snapshot requested after the last of
// those events has been delivered (docs/compositor-adapters.md, "Landed
// barrier").
//
// Bindings: a `binding` event whose command starts with `nop views-shell `
// goes to Delegate::OnBinding with the rest of the command.
//
// Reconnect: when either connection drops, pending commands fail with
// kNotConnected, the delegate hears OnDisconnected (if the adapter had
// connected), and a new client is made after a backoff that doubles up to
// `max_backoff`. The first snapshot after a reconnect is a full resync.

#ifndef VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_ADAPTER_H_
#define VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_ADAPTER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "views_shell/wm/adapters/scroll/scroll_ipc_client.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell::scroll {

// The event names the adapter subscribes to, in order.
const std::vector<std::string>& AdapterSubscriptions();

// The capability set for a GET_VERSION reply: the static table of
// docs/compositor-adapters.md for the variant ("scroll" when `variant` says
// so, "sway" otherwise), with the conditional hook rows (H2, H3, H4) kept only
// when the reply carries a `features` array naming them, by capability name
// or by hook id. Sets `*name` to "scroll" or "sway".
CapabilitySet ProbeCapabilities(const base::DictValue& version,
                                std::string* name);

// Folds GET_OUTPUTS, GET_WORKSPACES and GET_TREE replies into a snapshot.
// Windows are the tree's leaves (views), workspace-scoped by their workspace
// node, empty-workspaced in the scratchpad. `scroll_columns` fills
// WmWindow::column. `mru` is the focus-stack order (each node's `focus` array,
// depth first), which the adapter then merges with focus events.
WmSnapshot BuildSnapshot(const base::Value& outputs,
                         const base::Value& workspaces,
                         const base::Value& tree,
                         bool scroll_columns);

// The RUN_COMMAND text for `command` against `snapshot` (ids resolve to the
// names the compositor's commands take), or nullopt when an id is unknown or
// a required name is missing. The only place command strings are spelled.
std::optional<std::string> CommandText(const WmCommand& command,
                                       const WmSnapshot& snapshot);

class ScrollAdapter : public CompositorAdapter,
                      public ScrollIpcClient::Observer {
 public:
  struct Options {
    Options();
    Options(const Options&);
    Options& operator=(const Options&);
    ~Options();

    // Empty: ScrollIpcClient::ResolveSocketPath() at every connect.
    std::string socket_path;
    base::TimeDelta initial_backoff = base::Milliseconds(250);
    base::TimeDelta max_backoff = base::Seconds(10);
    // From RUN_COMMAND to the delivered echo; past it the command fails with
    // kNoEcho.
    base::TimeDelta command_timeout = base::Seconds(5);
  };

  explicit ScrollAdapter(Options options);
  ScrollAdapter(const ScrollAdapter&) = delete;
  ScrollAdapter& operator=(const ScrollAdapter&) = delete;
  ~ScrollAdapter() override;

  // CompositorAdapter:
  std::string_view name() const override;
  const CapabilitySet& capabilities() const override;
  void Start(Delegate* delegate) override;
  void Send(const WmCommand& command, CommandDone done) override;

  // The GET_VERSION reply of the current connection (empty before it).
  const base::DictValue& version() const { return version_; }
  // True from the first delivered snapshot of a connection to its loss.
  bool connected() const { return has_snapshot_; }

 private:
  struct PendingCommand {
    PendingCommand();
    PendingCommand(PendingCommand&&);
    PendingCommand& operator=(PendingCommand&&);
    ~PendingCommand();

    enum class State { kAwaitReply, kAwaitTick, kAwaitSnapshot };
    WmCommand::Kind kind = WmCommand::Kind::kFocusWorkspace;
    State state = State::kAwaitReply;
    std::string tick;
    uint64_t barrier_seq = 0;
    CommandDone done;
  };

  // ScrollIpcClient::Observer:
  void OnConnected() override;
  void OnEvent(uint32_t type, base::Value payload) override;
  void OnDisconnected() override;

  void Connect();
  void ResetConnectionState();
  void OnVersion(uint64_t generation, std::optional<base::Value> value);
  void RequestRefresh();
  void SendRefresh();
  void OnRefreshPart(uint64_t generation,
                     uint32_t type,
                     std::optional<base::Value> value);
  void DeliverSnapshot(WmSnapshot snapshot);
  void OnCommandReply(uint64_t generation,
                      uint64_t serial,
                      std::optional<base::Value> value);
  void OnTick(const base::DictValue& payload);
  void CheckBarriers();
  void OnCommandTimeout(uint64_t serial);
  void Finish(uint64_t serial, base::expected<void, WmCommandError> result);
  void FailAllCommands(WmCommandError error);

  SEQUENCE_CHECKER(sequence_checker_);
  const Options options_;
  raw_ptr<Delegate> delegate_ = nullptr;
  std::unique_ptr<ScrollIpcClient> client_;
  base::OneShotTimer reconnect_timer_;
  base::TimeDelta backoff_;

  // Per connection; reset on every (re)connect.
  uint64_t generation_ = 0;
  bool transport_up_ = false;
  bool probed_ = false;
  bool has_snapshot_ = false;
  base::DictValue version_;
  std::string name_ = "scroll";
  CapabilitySet capabilities_;
  // Counts workspace, window and output events; a refresh covers every event
  // counted before it was sent.
  uint64_t event_seq_ = 0;
  uint64_t delivered_seq_ = 0;
  bool refresh_in_flight_ = false;
  bool refresh_dirty_ = false;
  uint64_t refresh_seq_ = 0;
  std::optional<base::Value> refresh_outputs_;
  std::optional<base::Value> refresh_workspaces_;
  // Window ids, newest focus first, from `window` focus events.
  std::vector<std::string> mru_;
  // The last delivered snapshot: commands resolve ids against it.
  WmSnapshot current_;

  uint64_t next_serial_ = 1;
  std::map<uint64_t, PendingCommand> pending_;

  base::WeakPtrFactory<ScrollAdapter> weak_factory_{this};
};

}  // namespace views_shell::scroll

#endif  // VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_ADAPTER_H_
