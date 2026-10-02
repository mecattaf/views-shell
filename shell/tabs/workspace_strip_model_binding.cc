// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/tabs/workspace_strip_model_binding.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/expected.h"
#include "views_shell/tabs/workspace_strip.h"
#include "views_shell/wm/compositor_adapter.h"
#include "views_shell/wm/wm_snapshot.h"

namespace views_shell {

WorkspaceStripModelBinding::WorkspaceStripModelBinding(WmModel* model)
    : model_(model) {
  observation_.Observe(model_.get());
}

WorkspaceStripModelBinding::~WorkspaceStripModelBinding() = default;

std::unique_ptr<WorkspaceStrip> WorkspaceStripModelBinding::MakeStrip() {
  CHECK(!strip_);
  auto strip = std::make_unique<WorkspaceStrip>(
      base::BindRepeating(&WorkspaceStripModelBinding::Focus,
                          weak_ptr_factory_.GetWeakPtr()),
      WorkspaceStrip::SelectionMode::kRequest);
  strip_ = strip.get();
  if (model_->has_snapshot()) {
    Apply();
  }
  return strip;
}

// static
std::vector<Workspace> WorkspaceStripModelBinding::WorkspacesFromSnapshot(
    const WmSnapshot& snapshot) {
  std::vector<Workspace> workspaces;
  workspaces.reserve(snapshot.workspaces.size());
  for (const WmWorkspace& workspace : snapshot.workspaces) {
    workspaces.push_back(
        {.id = workspace.id,
         .title = base::UTF8ToUTF16(
             workspace.name.empty() ? workspace.id : workspace.name),
         .active = workspace.focused});
  }
  return workspaces;
}

void WorkspaceStripModelBinding::OnSnapshotApplied() {
  if (strip_) {
    Apply();
  }
}

void WorkspaceStripModelBinding::OnDisconnected() {
  // The model keeps its last snapshot until the reconnect's full snapshot
  // replaces it; so does the strip.
  LOG(WARNING) << "left-tabs: compositor connection lost; workspaces frozen "
                  "until it is back";
}

void WorkspaceStripModelBinding::Focus(const Workspace& workspace) {
  // Rule R6: ask, and wait for the echo. The strip redraws from
  // OnSnapshotApplied, never from here.
  LOG(INFO) << "left-tabs: FocusWorkspace " << workspace.id << " ("
            << base::UTF16ToUTF8(workspace.title) << ")";
  model_->FocusWorkspace(
      workspace.id,
      base::BindOnce(
          [](const std::string& workspace,
             base::expected<void, WmCommandError> result) {
            LOG_IF(WARNING, !result.has_value())
                << "left-tabs: focusing workspace " << workspace
                << " failed: " << WmCommandErrorName(result.error());
          },
          workspace.id));
}

void WorkspaceStripModelBinding::Apply() {
  ++apply_count_;
  std::vector<Workspace> workspaces =
      WorkspacesFromSnapshot(model_->snapshot());
  std::string active = "none";
  for (const Workspace& workspace : workspaces) {
    if (workspace.active) {
      active = base::UTF16ToUTF8(workspace.title);
    }
  }
  LOG(INFO) << "left-tabs: " << workspaces.size()
            << " workspaces from the compositor model, active " << active;
  strip_->SetWorkspaces(std::move(workspaces));
}

}  // namespace views_shell
