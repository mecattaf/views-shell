// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The compositor-neutral state an adapter publishes: one full snapshot at a
// time, never a patch (docs/compositor-adapters.md). Ids are adapter-scoped
// strings. The scroll adapter uses scroll's node ids, which are unique across
// every node of one compositor session and never reused within it
// (docs/architecture.md §6), so a workspace id and a window id never collide.
// Outputs are keyed by connector name. The out-of-line special members live in
// wm_model.cc (the Chromium style checker wants them out of line).

#ifndef VIEWS_SHELL_WM_WM_SNAPSHOT_H_
#define VIEWS_SHELL_WM_WM_SNAPSHOT_H_

#include <optional>
#include <string>
#include <vector>

namespace views_shell {

struct WmOutput {
  WmOutput();
  WmOutput(const WmOutput&);
  WmOutput(WmOutput&&);
  WmOutput& operator=(const WmOutput&);
  WmOutput& operator=(WmOutput&&);
  ~WmOutput();
  bool operator==(const WmOutput&) const;

  std::string id;    // connector name, e.g. "DP-1"
  std::string name;  // make and model, for display
  int width_px = 0;
  int height_px = 0;
  double scale = 1.0;
  bool focused = false;
};

struct WmWorkspace {
  WmWorkspace();
  WmWorkspace(const WmWorkspace&);
  WmWorkspace(WmWorkspace&&);
  WmWorkspace& operator=(const WmWorkspace&);
  WmWorkspace& operator=(WmWorkspace&&);
  ~WmWorkspace();
  bool operator==(const WmWorkspace&) const;

  std::string id;        // adapter-scoped and stable while the workspace lives
  std::string name;      // may be empty
  int index = 0;         // position within its output
  std::string output;    // WmOutput::id
  bool focused = false;  // the one focused workspace of the session
  bool active = false;   // the visible workspace of its output
  bool urgent = false;
};

struct WmWindow {
  WmWindow();
  WmWindow(const WmWindow&);
  WmWindow(WmWindow&&);
  WmWindow& operator=(const WmWindow&);
  WmWindow& operator=(WmWindow&&);
  ~WmWindow();
  bool operator==(const WmWindow&) const;

  std::string id;           // adapter-scoped
  std::string toplevel_id;  // ext-foreign-toplevel-list identifier, when known
  std::string app_id;
  std::string title;
  // WmWorkspace::id; empty for a window on no workspace (the scratchpad).
  std::string workspace;
  bool focused = false;
  bool fullscreen = false;
  bool urgent = false;
  // scroll only: the index of the column (the workspace's tiling child) that
  // holds the window. Unset for floating windows and on sway.
  std::optional<int> column;
};

struct WmSnapshot {
  WmSnapshot();
  WmSnapshot(const WmSnapshot&);
  WmSnapshot(WmSnapshot&&);
  WmSnapshot& operator=(const WmSnapshot&);
  WmSnapshot& operator=(WmSnapshot&&);
  ~WmSnapshot();
  bool operator==(const WmSnapshot&) const;

  // Lookups by id; nullptr when absent.
  const WmWorkspace* FindWorkspace(const std::string& id) const;
  const WmWindow* FindWindow(const std::string& id) const;
  // The focused workspace and window; nullptr when none.
  const WmWorkspace* FocusedWorkspace() const;
  const WmWindow* FocusedWindow() const;

  std::vector<WmOutput> outputs;
  // In the compositor's order: by output, then by position on the output.
  std::vector<WmWorkspace> workspaces;
  // In tree order within each workspace.
  std::vector<WmWindow> windows;
  // Most-recently-used window ids, newest first, recovered at start from the
  // compositor's focus stacks and maintained from focus events.
  std::vector<std::string> mru;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_WM_WM_SNAPSHOT_H_
