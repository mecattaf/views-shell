// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Where the workspace strip gets its workspaces. One snapshot per Fetch():
// a static list, or `niri msg -j workspaces` when niri is the compositor.
// Neither subscribes to change events yet; the compositor adapter
// (wm/compositor_adapter.h) will own that.

#ifndef VIEWS_SHELL_TABS_WORKSPACE_SOURCE_H_
#define VIEWS_SHELL_TABS_WORKSPACE_SOURCE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"

namespace views_shell {

struct Workspace {
  // The compositor's id for the workspace (niri: the numeric id).
  std::string id;
  // What the tab shows.
  std::u16string title;
  // The focused workspace. At most one workspace in a snapshot is active.
  bool active = false;

  bool operator==(const Workspace& other) const = default;
};

class WorkspaceSource {
 public:
  using FetchCallback = base::OnceCallback<void(std::vector<Workspace>)>;

  virtual ~WorkspaceSource() = default;

  // A short name for logs: "static" or "niri".
  virtual std::string_view name() const = 0;

  // Takes one snapshot. `callback` runs on the calling sequence; an empty
  // vector means the source had nothing (or failed, which it logs).
  virtual void Fetch(FetchCallback callback) = 0;
};

// Returns `workspaces` on every Fetch().
std::unique_ptr<WorkspaceSource> CreateStaticWorkspaceSource(
    std::vector<Workspace> workspaces);

// The four-workspace list the --left-tabs demo shows without a compositor.
std::vector<Workspace> DemoWorkspaces();

// Runs `niri msg -j workspaces` off the UI thread and parses it.
std::unique_ptr<WorkspaceSource> CreateNiriWorkspaceSource();

// Parses the JSON array `niri msg -j workspaces` prints. Workspaces are
// ordered by output name, then by index on that output. The title is the
// workspace name, else its index; with more than one output the output name
// follows in parentheses. The focused workspace is active. Malformed input
// gives an empty vector.
std::vector<Workspace> ParseNiriWorkspaces(std::string_view json);

// `name` is "static", "niri" or empty. Empty picks niri when NIRI_SOCKET is
// set and the static demo list otherwise. Returns nullptr for an unknown name.
std::unique_ptr<WorkspaceSource> CreateWorkspaceSource(std::string_view name);

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_WORKSPACE_SOURCE_H_
