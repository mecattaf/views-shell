// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SKETCH. Never compiled. The compositor-neutral state an adapter publishes.

#ifndef VIEWS_SHELL_WM_WM_SNAPSHOT_H_
#define VIEWS_SHELL_WM_WM_SNAPSHOT_H_

#include <optional>
#include <string>
#include <vector>

namespace views-shell {

struct WmOutput {
  std::string id;    // connector name, e.g. "DP-1"
  std::string name;  // make/model for display
  int width_px = 0;
  int height_px = 0;
  double scale = 1.0;
  bool focused = false;
};

struct WmWorkspace {
  std::string id;      // adapter-scoped and stable while the workspace lives
  std::string name;    // may be empty
  int index = 0;       // position within its output
  std::string output;  // WmOutput::id
  bool focused = false;  // the one focused workspace of the session
  bool active = false;   // the visible workspace of its output
  bool urgent = false;
};

struct WmWindow {
  std::string id;            // adapter-scoped
  std::string toplevel_id;   // ext-foreign-toplevel-list identifier, when known
  std::string app_id;
  std::string title;
  std::string workspace;     // WmWorkspace::id; empty when scratchpad/floating-off-tree
  bool focused = false;
  bool fullscreen = false;
  bool urgent = false;
  std::optional<int> column;  // scroll.scroller decoration, when declared
};

struct WmSnapshot {
  std::vector<WmOutput> outputs;
  std::vector<WmWorkspace> workspaces;
  std::vector<WmWindow> windows;
  // Most-recently-used window ids, newest first, recovered at start from the
  // compositor's focus stacks and maintained from focus events.
  std::vector<std::string> mru;
};

}  // namespace views-shell

#endif  // VIEWS_SHELL_WM_WM_SNAPSHOT_H_
