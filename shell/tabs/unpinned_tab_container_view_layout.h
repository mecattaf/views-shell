// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/common/unpinned_tab_container_view_layout.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_UNPINNED_TAB_CONTAINER_VIEW_LAYOUT_H_
#define VIEWS_SHELL_TABS_UNPINNED_TAB_CONTAINER_VIEW_LAYOUT_H_

#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/layout/proposed_layout.h"

namespace views_shell {

// Layout manager that stacks the host's children top to bottom, as Chrome's
// vertical tab strip lays out its unpinned tabs. Only the vertical half of
// Chrome's class is ported, and the host's direct children stand in for the
// tab collection node.
class UnpinnedTabContainerViewLayout : public views::LayoutManagerBase {
 public:
  UnpinnedTabContainerViewLayout();
  UnpinnedTabContainerViewLayout(const UnpinnedTabContainerViewLayout&) =
      delete;
  UnpinnedTabContainerViewLayout& operator=(
      const UnpinnedTabContainerViewLayout&) = delete;
  ~UnpinnedTabContainerViewLayout() override;

  // views::LayoutManagerBase:
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override;
  gfx::Size GetMinimumSize(const views::View* host) const override;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_UNPINNED_TAB_CONTAINER_VIEW_LAYOUT_H_
