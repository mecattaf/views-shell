// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SKETCH. Never compiled. The interface every compositor adapter implements
// (docs/compositor-adapters.md). Template for the implementation:
// wm/adapters/niri/niri_ipc_client.* (lifted). The adapter runs its sockets on
// an IO sequence and posts full snapshots to the UI sequence; it never posts a
// patch, and it never assumes a command worked.

#ifndef VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_
#define VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "views_shell/wm/wm_snapshot.h"
#include "ui/gfx/geometry/rect.h"

namespace views-shell {

// Dotted capability names, family first: "workspaces.focus", "scroll.lua".
using Capability = std::string;
using CapabilitySet = base::flat_set<Capability>;

// A typed request. Raw compositor command strings never cross this boundary
// from plugins, the CLI or Chrome; only adapters spell them.
struct WmCommand {
  enum class Kind {
    kFocusWorkspace,
    kFocusWindow,
    kRenameWorkspace,
    kMoveWindowToWorkspace,
    kToggleScratchpad,
    kSessionExit,
    kReloadConfig,
  };
  Kind kind;
  std::string workspace;  // adapter-scoped workspace id
  std::string window;     // adapter-scoped window id
  std::string name;       // for rename
};

enum class WmCommandError {
  kNotConnected,
  kCapabilityMissing,
  kRejected,  // the compositor answered with an error
  kNoEcho,    // the command was accepted but the barrier never arrived
};

class CompositorAdapter {
 public:
  struct Delegate {
    virtual ~Delegate() = default;
    // A full snapshot: outputs, workspaces, windows, focus, fullscreen, urgency.
    // Also sent first on every (re)connect.
    virtual void OnSnapshot(WmSnapshot snapshot) = 0;
    // scroll and sway only: a `nop views-shell …` binding fired.
    virtual void OnBinding(std::string_view payload) = 0;
    // The compositor reloaded its config (success or failure).
    virtual void OnConfigReloaded(bool ok, std::string_view error) = 0;
    virtual void OnDisconnected() = 0;

    // Optional hook events (docs/scroll-fork-hooks.md). Delivered only when the
    // matching capability is present; the defaults ignore them, so an adapter
    // for a compositor without the hook never calls them.
    enum class JumpPhase { kBegin, kEnd };
    struct JumpLabel {
      std::string window;  // adapter-scoped window id
      std::string label;
      gfx::Rect rect;
    };
    // scroll.jump with hook H1: scroll owns the keyboard between kBegin and kEnd.
    virtual void OnJump(JumpPhase phase, std::vector<JumpLabel> labels) {}
    // overview.events (hook H2 on scroll; niri without geometry).
    virtual void OnOverview(bool enabled,
                            std::vector<std::pair<std::string, gfx::Rect>>
                                workspace_rects) {}
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
  // listed as conditional and kept only when the probe confirms them (on scroll:
  // the GET_VERSION `features` array, hook H10).
  virtual const CapabilitySet& capabilities() const = 0;

  // Connect, subscribe before querying, then deliver the first snapshot.
  // Reconnects with backoff on its own.
  virtual void Start(Delegate* delegate) = 0;

  // Sends a typed command and calls back once the compositor's echo for it has
  // been delivered to the delegate (scroll/sway: a SEND_TICK barrier; niri: the
  // Handled reply followed by the matching event; Hyprland: "ok" then socket2).
  // Callers redraw on the echo, not on this callback.
  using CommandDone = base::OnceCallback<void(base::expected<void, WmCommandError>)>;
  virtual void Send(const WmCommand& command, CommandDone done) = 0;
};

}  // namespace views-shell

#endif  // VIEWS_SHELL_WM_COMPOSITOR_ADAPTER_H_
