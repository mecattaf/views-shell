// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/bar_view.h
//
// agency::BarView -- the top-bar Views tree (Surface 1). A three-zone
// horizontal strip (left / center / right) hosted on a wlr-layer-shell surface
// via the generalized name-keyed weld in
// ui/views/widget/desktop_aura/desktop_window_tree_host_linux.cc (widget name
// "agency:bar", see topbar-design.md sections 1 and 4).
//
// Token-clean: the bar background and clock label colour are M3 semantic
// ColorProvider ids (ui::kColorSysHeader / ui::kColorSysOnSurface), never raw
// hex; the single layout constant kBarHeightDip is cross-referenced to the
// weld table's exclusive_zone. No fonts, no theming, stock look.
//
// This file names NO Ozone layer-shell type -- the weld is entirely in
// //ui/views -- so the shell target compiles unconditionally, independent of
// BUILDFLAG(ENABLE_COWL_LAYER_SHELL).

#ifndef AGENCY_SHELL_BAR_VIEW_H_
#define AGENCY_SHELL_BAR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace gfx {
class Size;
}  // namespace gfx

namespace views {
class Label;
class SizeBounds;
}  // namespace views

namespace agency {

// The bar's height in DIP. DUPLICATED (by documented design, topbar-design.md
// section 1) with the weld table's exclusive_zone literal in
// desktop_window_tree_host_linux.cc -- keep the two in sync until the v1.1
// kAuto sentinel lands.
inline constexpr int kBarHeightDip = 36;

// The top-bar contents view. Owned by the bar Widget (SetContentsView); its
// clock_label() is driven by a ClockController held by ShellHost.
class BarView : public views::View {
 public:
  BarView();
  BarView(const BarView&) = delete;
  BarView& operator=(const BarView&) = delete;
  ~BarView() override;

  // The center-zone clock label. Non-owning; owned by this view's subtree.
  // Handed to ClockController so it can push formatted wall-clock text.
  views::Label* clock_label() { return clock_label_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  raw_ptr<views::View> left_zone_ = nullptr;    // placeholder (empty for MVP)
  raw_ptr<views::View> center_zone_ = nullptr;  // holds the clock label
  raw_ptr<views::View> right_zone_ = nullptr;   // placeholder (tray descoped)
  raw_ptr<views::Label> clock_label_ = nullptr;
};

}  // namespace agency

#endif  // AGENCY_SHELL_BAR_VIEW_H_
