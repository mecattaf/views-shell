// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Feeds a WorkspaceStrip (the ported Chrome tab strip) from a WmModel, the
// compositor adapter's model (wm/wm_model.h), and sends its selections back.
//
// Rule R6 (command, then observed echo): the strip draws only what the model
// reports. A selected tab calls WmModel::FocusWorkspace and changes nothing;
// the active tab moves when the compositor's echo arrives as a snapshot and
// the model calls OnSnapshotApplied, which rebuilds the strip from that
// snapshot, and from no other path. This is the bar's workspace strip's
// contract (bar/workspace_strip.h) on Chrome's tabs.
#ifndef VIEWS_SHELL_TABS_WORKSPACE_STRIP_MODEL_BINDING_H_
#define VIEWS_SHELL_TABS_WORKSPACE_STRIP_MODEL_BINDING_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/types/expected.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/tabs/workspace_source.h"
#include "views_shell/wm/wm_model.h"

namespace views_shell {

class WorkspaceStrip;
struct WmSnapshot;

class WorkspaceStripModelBinding : public WmModel::Observer {
 public:
  // `model` must outlive the binding. The strip comes from MakeStrip().
  explicit WorkspaceStripModelBinding(WmModel* model);
  WorkspaceStripModelBinding(const WorkspaceStripModelBinding&) = delete;
  WorkspaceStripModelBinding& operator=(const WorkspaceStripModelBinding&) =
      delete;
  ~WorkspaceStripModelBinding() override;

  // The strip this binding feeds, in SelectionMode::kRequest, holding the
  // snapshot the model already has (the model's state, not a guess). Called
  // once; the caller owns the view and must not keep it past the binding.
  std::unique_ptr<WorkspaceStrip> MakeStrip();

  // One Workspace per workspace of the snapshot, in the snapshot's order:
  // the id, the name (or the id when the name is empty) as the title, and
  // the session's focused workspace as the active one.
  static std::vector<Workspace> WorkspacesFromSnapshot(
      const WmSnapshot& snapshot);

  WorkspaceStrip* strip() { return strip_; }
  // How many snapshots were applied to the strip.
  int apply_count() const { return apply_count_; }

  // WmModel::Observer:
  void OnSnapshotApplied() override;
  void OnDisconnected() override;

 private:
  // The strip's SelectCallback: the request.
  void Focus(const Workspace& workspace);
  // The adapter's answer. A refusal clears the strip's pending request.
  void OnFocusDone(const std::string& workspace,
                   base::expected<void, WmCommandError> result);
  void Apply();

  const raw_ptr<WmModel> model_;
  raw_ptr<WorkspaceStrip> strip_ = nullptr;
  int apply_count_ = 0;
  base::ScopedObservation<WmModel, WmModel::Observer> observation_{this};
  base::WeakPtrFactory<WorkspaceStripModelBinding> weak_ptr_factory_{this};
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_WORKSPACE_STRIP_MODEL_BINDING_H_
