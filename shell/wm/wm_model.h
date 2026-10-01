// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SKETCH. Never compiled. The compositor-neutral mirror the surfaces observe.
// It diffs successive snapshots into the three callbacks Chrome's vertical tab
// collection uses (children added, removed, moved), so the rail's WorkspaceNode
// tree animates inserts in and removals out. Shaped after Ash's desk and MRU
// observers. Replaces the state fold in the lifted rail/rail_controller.cc.

#ifndef VIEWS_SHELL_WM_WM_MODEL_H_
#define VIEWS_SHELL_WM_WM_MODEL_H_

#include <string>

#include "base/observer_list.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/wm/wm_snapshot.h"

namespace views-shell {

class WmModel : public CompositorAdapter::Delegate {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // A workspace (parent empty) or a window (parent = workspace id) appeared.
    virtual void OnChildAdded(const std::string& parent, const std::string& id, int index) {}
    virtual void OnChildRemoved(const std::string& parent, const std::string& id) {}
    virtual void OnChildMoved(const std::string& old_parent, const std::string& new_parent,
                              const std::string& id, int new_index) {}
    // Focus, name, urgency or fullscreen of an existing item changed.
    virtual void OnItemChanged(const std::string& id) {}
    virtual void OnFocusChanged(const std::string& workspace, const std::string& window) {}
  };

  explicit WmModel(CompositorAdapter* adapter);
  ~WmModel() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const WmSnapshot& snapshot() const { return current_; }
  const CapabilitySet& capabilities() const;

  // Requests go to the adapter. The model does not change until the echo
  // arrives as a new snapshot (rule R6: compositor command, then observed echo).
  void FocusWorkspace(const std::string& workspace);
  void FocusWindow(const std::string& window);
  void RenameWorkspace(const std::string& workspace, const std::string& name);
  void MoveWindow(const std::string& window, const std::string& workspace);

  // CompositorAdapter::Delegate:
  void OnSnapshot(WmSnapshot snapshot) override;  // diff `current_` -> observers
  void OnBinding(std::string_view payload) override;
  void OnConfigReloaded(bool ok, std::string_view error) override;
  void OnDisconnected() override;

 private:
  raw_ptr<CompositorAdapter> adapter_;
  WmSnapshot current_;
  base::ObserverList<Observer> observers_;
};

}  // namespace views-shell

#endif  // VIEWS_SHELL_WM_WM_MODEL_H_
