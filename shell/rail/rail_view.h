// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/rail_view.h
//
// agency::RailView -- Surface 5 (the left rail), v1 READ-ONLY MIRROR. A
// left-anchored vertical strip hosted on a wlr-layer-shell surface via the
// name-keyed weld in ui/views/widget/desktop_aura/desktop_window_tree_host_
// linux.cc (widget name "agency:rail"). It renders niri's live workspace/window
// list -- one header row per workspace, one indented row per window -- driven
// by a RailController that folds the NiriIpcClient event stream into a
// RailModel and pushes it via SetModel().
//
// SCOPE (v1, partial by design, issue #10): read-only mirror ONLY. There is NO
// TabStrip, NO TabStripController, NO drag/tear-off, NO click-to-focus. Those
// are the large Chromium-tabstrip-adapter lift measured in
// july16-morning/thread1/grounding/rail-tabstrip-seam.md and are explicitly out
// of tonight's increment. This view is a plain views::View tree of Labels.
//
// Token-clean: background is ui::kColorSysHeader, all text is
// ui::kColorSysOnSurface (the two M3 semantic ids proven present @150 by
// bar_view.cc). Emphasis (active workspace / focused window) is carried by
// text-prefix glyphs rather than unverified colour tokens, so a theme with a
// missing accent id can never fail the build or wash the emphasis out.
//
// This file names NO Ozone layer-shell type -- the weld is entirely in
// //ui/views -- so the shell target compiles unconditionally.

#ifndef AGENCY_SHELL_RAIL_VIEW_H_
#define AGENCY_SHELL_RAIL_VIEW_H_

#include <string>
#include <vector>

#include "ui/views/view.h"

namespace gfx {
class Size;
}  // namespace gfx

namespace views {
class SizeBounds;
}  // namespace views

namespace agency {

// The rail's width in DIP. DUPLICATED (by documented design, mirroring the bar)
// with the weld table's exclusive_zone literal for "agency:rail" in
// desktop_window_tree_host_linux.cc (patches/agency-layer-shell-weld.patch) --
// keep the two in sync.
inline constexpr int kRailWidthDip = 240;

// One window row under a workspace.
struct RailWindowItem {
  std::string label;         // title, or app_id, or "window <id>" fallback
  bool is_focused = false;   // niri keyboard focus is on this window
};

// One workspace header row plus its windows.
struct RailWorkspaceItem {
  std::string label;         // niri name, or "ws <idx>" fallback
  bool is_active = false;    // active workspace on its output
  bool is_urgent = false;    // urgency hint set
  std::vector<RailWindowItem> windows;
};

// The whole rail's render input -- a flattened, UI-ready projection the
// RailController builds from the retained niri workspace/window vectors.
struct RailModel {
  std::vector<RailWorkspaceItem> workspaces;
};

// The rail contents view. Owned by the rail Widget (SetContentsView); its
// SetModel() is called by RailController on every folded niri update (and once
// with the test-only static snapshot under --agency-rail-test-snapshot).
class RailView : public views::View {
 public:
  RailView();
  RailView(const RailView&) = delete;
  RailView& operator=(const RailView&) = delete;
  ~RailView() override;

  // Rebuild the row tree from `model`. Cheap-and-correct: clears every child
  // and re-adds Labels. v1 renders at most a few dozen rows, so a full rebuild
  // per niri event is well within budget and avoids a diff/keyed-update engine
  // this partial does not need.
  void SetModel(const RailModel& model);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
};

}  // namespace agency

#endif  // AGENCY_SHELL_RAIL_VIEW_H_
