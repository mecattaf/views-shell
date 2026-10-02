// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The compositor-neutral mirror the surfaces observe. It diffs successive
// snapshots into the three callbacks Chrome's vertical tab collection uses
// (children added, removed, moved), so a rail's workspace tree animates
// inserts in and removals out. Shaped after Ash's desk and MRU observers.
//
// The tree has two levels. Workspaces are the children of the root (parent
// ""), in snapshot order. Windows are the children of their workspace, in
// tree order; windows on no workspace (the scratchpad) are the children of
// kScratchpadParent. Ids are the adapter's, unique across both levels.
//
// The model is read-mostly: requests go to the adapter, and the model changes
// only when the echo arrives as a new snapshot (rule R6: compositor command,
// then observed echo). It is never a second writer of window state.

#ifndef VIEWS_SHELL_WM_WM_MODEL_H_
#define VIEWS_SHELL_WM_WM_MODEL_H_

#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/wm/wm_snapshot.h"

namespace views_shell {

class WmModel : public CompositorAdapter::Delegate {
 public:
  // The parent of windows that are on no workspace.
  static constexpr std::string_view kScratchpadParent = "@scratchpad";

  // Diff callbacks for one snapshot come in this order: windows removed;
  // workspaces added or moved (indexes against the root's list as it stands
  // at that call); windows added or moved, workspace by workspace; workspaces
  // removed; then OnItemChanged, OnOutputsChanged, OnMruChanged and
  // OnFocusChanged; and last OnSnapshotApplied. Indexes are positions in the
  // parent's list after the call, so an observer that applies the calls in
  // order holds the snapshot's order at the end.
  class Observer : public base::CheckedObserver {
   public:
    // A workspace (parent "") or a window (parent = workspace id) appeared.
    virtual void OnChildAdded(const std::string& parent,
                              const std::string& id,
                              int index) {}
    virtual void OnChildRemoved(const std::string& parent,
                                const std::string& id) {}
    // Within one parent (old_parent == new_parent) or between two.
    virtual void OnChildMoved(const std::string& old_parent,
                              const std::string& new_parent,
                              const std::string& id,
                              int new_index) {}
    // Name, output, focus, visibility, urgency or fullscreen of an existing
    // workspace or window changed (title and app id too, for a window).
    virtual void OnItemChanged(const std::string& id) {}
    virtual void OnOutputsChanged() {}
    virtual void OnMruChanged() {}
    // The focused workspace or window changed; either may be empty.
    virtual void OnFocusChanged(const std::string& workspace,
                                const std::string& window) {}
    // A snapshot was applied (also when nothing differed).
    virtual void OnSnapshotApplied() {}
    virtual void OnBinding(std::string_view payload) {}
    virtual void OnConfigReloaded(bool ok, std::string_view error) {}
    // The adapter lost the compositor. The model keeps the last snapshot until
    // the reconnect's full snapshot replaces it.
    virtual void OnDisconnected() {}
  };

  // `adapter` must outlive the model. The model does not call Start().
  explicit WmModel(CompositorAdapter* adapter);
  WmModel(const WmModel&) = delete;
  WmModel& operator=(const WmModel&) = delete;
  ~WmModel() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const WmSnapshot& snapshot() const { return current_; }
  const CapabilitySet& capabilities() const;
  // True once a snapshot has been applied.
  bool has_snapshot() const { return has_snapshot_; }

  // Requests go to the adapter. The model does not change until the echo
  // arrives as a new snapshot. `done` is optional.
  void FocusWorkspace(const std::string& workspace,
                      CompositorAdapter::CommandDone done = {});
  // Focuses the workspace named `name`, creating it when the compositor does
  // that (scroll and sway do).
  void FocusWorkspaceByName(const std::string& name,
                            CompositorAdapter::CommandDone done = {});
  void FocusWindow(const std::string& window,
                   CompositorAdapter::CommandDone done = {});
  void RenameWorkspace(const std::string& workspace,
                       const std::string& name,
                       CompositorAdapter::CommandDone done = {});
  void MoveWindow(const std::string& window,
                  const std::string& workspace,
                  CompositorAdapter::CommandDone done = {});
  void ToggleScratchpad(CompositorAdapter::CommandDone done = {});
  void ReloadConfig(CompositorAdapter::CommandDone done = {});
  void ExitSession(CompositorAdapter::CommandDone done = {});

  // CompositorAdapter::Delegate:
  void OnSnapshot(WmSnapshot snapshot) override;
  void OnBinding(std::string_view payload) override;
  void OnConfigReloaded(bool ok, std::string_view error) override;
  void OnDisconnected() override;

 private:
  void Send(WmCommand command, CompositorAdapter::CommandDone done);

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<CompositorAdapter> adapter_;
  WmSnapshot current_;
  bool has_snapshot_ = false;
  base::ObserverList<Observer> observers_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_WM_WM_MODEL_H_
