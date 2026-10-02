// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The interface every compositor adapter implements
// (docs/compositor-adapters.md). An adapter runs its sockets on its own IO
// thread and delivers full snapshots to the sequence that called Start(); it
// never delivers a patch, and it never assumes a command worked: a command is
// done only when its echo has been delivered as a snapshot.
//
// The first implementation is adapters/scroll/scroll_adapter.h (scroll and
// sway, over i3-ipc).

#ifndef VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_
#define VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "ui/gfx/geometry/rect.h"
#include "views_shell/wm/wm_snapshot.h"

namespace views_shell {

// Dotted capability names, family first: "workspaces.focus", "scroll.lua".
using Capability = std::string;
using CapabilitySet = base::flat_set<Capability>;

// A typed request. Raw compositor command strings never cross this boundary
// from plugins, the CLI or Chrome; only adapters spell them.
struct WmCommand {
  enum class Kind {
    kFocusWorkspace,         // workspace (an id), or name when workspace is empty
    kFocusWindow,            // window
    kRenameWorkspace,        // workspace, name (the new name)
    kMoveWindowToWorkspace,  // window, and workspace or (when empty) name
    kToggleScratchpad,       // no arguments
    kSessionExit,            // no arguments
    kReloadConfig,           // no arguments
  };

  WmCommand();
  explicit WmCommand(Kind kind);
  WmCommand(const WmCommand&);
  WmCommand(WmCommand&&);
  WmCommand& operator=(const WmCommand&);
  WmCommand& operator=(WmCommand&&);
  ~WmCommand();

  Kind kind = Kind::kFocusWorkspace;
  std::string workspace;  // adapter-scoped workspace id
  std::string window;     // adapter-scoped window id
  std::string name;       // a workspace name (rename target, or by-name focus)
};

// The capability a command needs; the adapter refuses the command without it.
Capability RequiredCapability(WmCommand::Kind kind);

enum class WmCommandError {
  kNotConnected,
  kCapabilityMissing,
  kInvalidArgument,  // an id the current snapshot does not know, or a missing name
  kRejected,         // the compositor answered with an error
  kNoEcho,           // the command was accepted but the barrier never arrived
};

std::string_view WmCommandErrorName(WmCommandError error);

class CompositorAdapter {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // A full snapshot: outputs, workspaces, windows, focus, fullscreen, urgency.
    // Also sent first on every (re)connect.
    virtual void OnSnapshot(WmSnapshot snapshot) = 0;
    // scroll and sway only: a `nop views-shell <payload>` binding fired.
    virtual void OnBinding(std::string_view payload) = 0;
    // The compositor reloaded its config (ok), or refused a reload asked for
    // through kReloadConfig (not ok, with the compositor's error).
    virtual void OnConfigReloaded(bool ok, std::string_view error) = 0;
    // A connection that had delivered OnConnected-level state was lost. The
    // adapter reconnects on its own and sends a full snapshot when it is back.
    virtual void OnDisconnected() = 0;

    // Optional hook events (docs/scroll-fork-hooks.md). Delivered only when the
    // matching capability is present; the defaults ignore them, so an adapter
    // for a compositor without the hook never calls them.
    enum class JumpPhase { kBegin, kEnd };
    struct JumpLabel {
      JumpLabel();
      JumpLabel(const JumpLabel&);
      JumpLabel(JumpLabel&&);
      JumpLabel& operator=(const JumpLabel&);
      JumpLabel& operator=(JumpLabel&&);
      ~JumpLabel();

      std::string window;  // adapter-scoped window id
      std::string label;
      gfx::Rect rect;
    };
    // scroll.jump with hook H1: scroll owns the keyboard between kBegin and kEnd.
    virtual void OnJump(JumpPhase phase, std::vector<JumpLabel> labels) {}
    // overview.events (hook H2 on scroll; niri without geometry).
    virtual void OnOverview(
        bool enabled,
        std::vector<std::pair<std::string, gfx::Rect>> workspace_rects) {}
    // windows.geometry-events (hook H3 on scroll; niri WindowLayoutsChanged).
    // One call per applied compositor transaction, changed windows only.
    virtual void OnGeometry(
        std::vector<std::pair<std::string, gfx::Rect>> changed_windows) {}
  };

  virtual ~CompositorAdapter() = default;

  // "scroll", "sway", "niri", "hyprland". For display only: plugins and the
  // core query capabilities, never this name.
  virtual std::string_view name() const = 0;

  // The static declaration, narrowed by the probe at connect. A probe never
  // invents a capability the static table does not list. Hook capabilities are
  // listed as conditional and kept only when the probe confirms them (on
  // scroll: the GET_VERSION `features` array, hook H10). Empty until the
  // first connect has been probed.
  virtual const CapabilitySet& capabilities() const = 0;

  // Connect, subscribe before querying, then deliver the first snapshot.
  // Reconnects with backoff on its own. `delegate` must outlive the adapter.
  virtual void Start(Delegate* delegate) = 0;

  // Sends a typed command and calls back once the compositor's echo for it has
  // been delivered to the delegate (scroll/sway: a SEND_TICK barrier; niri: the
  // Handled reply followed by the matching event; Hyprland: "ok" then socket2).
  // Callers redraw on the echo, not on this callback. `done` never runs
  // synchronously inside Send().
  using CommandDone =
      base::OnceCallback<void(base::expected<void, WmCommandError>)>;
  virtual void Send(const WmCommand& command, CommandDone done) = 0;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_
