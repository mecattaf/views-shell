// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_VIEW_VERTICAL_LAYOUT_H_
#define VIEWS_SHELL_TABS_TAB_VIEW_VERTICAL_LAYOUT_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/layout/proposed_layout.h"

namespace views_shell {

class Tab;

// Lays out a vertical tab's children: the close button at the trailing edge,
// the title filling the rest. In Chrome this is the layout of TabView, the
// vertical strip's tab class; views-shell installs it on the ported Tab.
class TabViewVerticalLayout : public views::LayoutManagerBase {
 public:
  TabViewVerticalLayout();
  TabViewVerticalLayout(const TabViewVerticalLayout&) = delete;
  TabViewVerticalLayout& operator=(const TabViewVerticalLayout&) = delete;
  ~TabViewVerticalLayout() override;

  // Chrome's widest uncollapsed vertical strip
  // (VerticalTabStripRegionView::kUncollapsedMaxWidth).
  static constexpr int kUncollapsedMaxWidth = 400;

  // TabView::UncollapsedMinWidth(): the width of a tab in a split in a group
  // while the strip is at its narrowest uncollapsed width.
  static int UncollapsedMinWidth();

 protected:
  // views::LayoutManagerBase:
  void OnInstalled(views::View* host) override;
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override;

 private:
  struct TabChildConfig {
    raw_ptr<views::View> view = nullptr;
    int min_width = 0;
    int padding = 0;
    bool align_leading = false;
    bool expand = false;
  };

  const Tab& tab() const;

  gfx::Rect GetChildBounds(const gfx::Rect& container,
                           const TabChildConfig& config,
                           const bool center) const;

  // Calculates the visibility of child view based on various states.
  bool IsChildVisible(const views::View* child, const int width) const;

  // Ordered vector of children to be rendered in the tab.
  std::vector<TabChildConfig> tab_children_configs_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_VIEW_VERTICAL_LAYOUT_H_
