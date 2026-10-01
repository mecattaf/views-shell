// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/launcher_panel.h
//
// PROVENANCE
// ----------
// Adapted from the RESCUED, spike-proven prototype
//   src/agency/shell/rescued/views_shell_launcher_panel.{h,cc}
// (itself lifted byte-identical from the CADE spike-0 workbench; see
// rescued/PROVENANCE.md). The proven Views tree -- translucent rounded panel,
// search Textfield on top, a separator, result rows (icon swatch +
// primary/secondary text + right action chip), a selected-row highlight, and a
// footer of key-cap hints -- is preserved. The spike (SPIKE-RESULT.md, verdict
// GO) proved this exact tree takes keyboard focus end-to-end on a niri
// zwlr_layer_surface_v1 overlay (layer=Overlay, keyboard=Exclusive).
//
// Deltas from the rescued spike (launcher-design.md §3):
//   D1  ShellLauncherRoot (full-screen transparent centering wrapper) DROPPED.
//       The anchor-none fixed-size layer surface centers the panel at the
//       compositor, so LauncherPanel is the widget's contents view directly.
//   D2  Hard-coded 5 rows + seed text "apple" REPLACED by live results from
//       LauncherProducer::Query() (browse mode on empty query).
//   D3  Enter now runs the Launch verb (LauncherProducer::Activate) and closes,
//       instead of logging a no-op.
//   D4  Icons are provider-colored swatches (real freedesktop icon-theme
//       loading from LauncherResult::icon is a follow-up).
//   D5  Results capped at kLauncherMaxRows (fixed-size surface; dynamic resize
//       is not plumbed above Ozone).
//   D6  No separate controller class -- the launcher has no observer/mojo seam;
//       the panel binds the producer by direct call, so LauncherPanel IS the
//       controller (preserves the rescued self-controller structure).
//
// Token-clean colors: every opaque color is an M3 semantic ui::ColorId. The
// ONE documented raw-color exception is the frosted-glass panel fill
// (kLauncherGlassFill), whose alpha is load-bearing for niri blur-through
// (spike signal #2) and has no opaque semantic token equivalent. See the .cc
// and VERIFIED-APIS.md.

#ifndef AGENCY_SHELL_LAUNCHER_PANEL_H_
#define AGENCY_SHELL_LAUNCHER_PANEL_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace ui {
class KeyEvent;
}  // namespace ui

namespace views {
class Textfield;
}  // namespace views

namespace agency {

class LauncherProducer;
struct LauncherResult;

// Fixed panel geometry (proven spike dims). DUPLICATED (by documented design,
// launcher-design.md §1/§2) with the weld row's fixed surface size in
// desktop_window_tree_host_linux.cc (patches/agency-layer-shell-weld.patch) --
// keep the two in sync. 399 = 58 (search) + 1 (separator) + 5*56 (rows) + 44
// (footer) + 16 (vertical insets).
inline constexpr int kLauncherPanelWidth = 760;
inline constexpr int kLauncherPanelHeight = 399;
inline constexpr int kLauncherMaxRows = 5;  // fixed surface => cap rows

// The Dia/Arc-style command palette: a translucent rounded panel with a search
// field on top, a live list of result rows, a selected-row highlight, and a
// footer of key-cap hints. Drives LauncherProducer directly (Query on every
// keystroke, Activate on Enter). It is its own views::TextfieldController.
class LauncherPanel : public views::View, public views::TextfieldController {
  METADATA_HEADER(LauncherPanel, views::View)

 public:
  // `producer` is not owned; it must outlive this panel (ShellHost teardown
  // order guarantees the widget -- which owns this panel and its
  // raw_ptr<LauncherProducer> -- dies before the producer).
  explicit LauncherPanel(LauncherProducer* producer);
  ~LauncherPanel() override;

  LauncherPanel(const LauncherPanel&) = delete;
  LauncherPanel& operator=(const LauncherPanel&) = delete;

  // views::TextfieldController:
  // Navigation keys drive the result list while focus stays in the field;
  // Enter activates the selection, Esc hides the panel.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  // Live filter: re-runs the query on every keystroke.
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  // Move the selected row by |delta| (wraps), repaint highlights.
  void MoveSelection(int delta);
  // Set the selected row to |index| (clamped).
  void SetSelection(int index);
  int selected_index() const { return selected_index_; }

  views::Textfield* search() { return search_; }
  // Close request from Esc -- hides the hosting widget.
  void RequestClose();

 private:
  // Re-run the producer query for `query`, rebuild the rows.
  void RebuildResults(const std::u16string& query);
  // Tear down the current result rows.
  void ClearRows();
  // Append one row for `r` (icon swatch + text column + action chip).
  void AddResultRow(const LauncherResult& r);
  // The Launch verb: routes the selected row back to its producer.
  void ActivateSelected();

  raw_ptr<LauncherProducer> producer_;               // not owned
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;  // rows parent (relayout)
  std::vector<raw_ptr<views::View>> rows_;
  std::vector<LauncherResult> results_;               // selected-row identity
  int selected_index_ = 0;
};

}  // namespace agency

#endif  // AGENCY_SHELL_LAUNCHER_PANEL_H_
