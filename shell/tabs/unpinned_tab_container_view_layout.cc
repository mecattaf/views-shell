// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/common/unpinned_tab_container_view_layout.cc @ 154.0.8037.92.

#include "views_shell/tabs/unpinned_tab_container_view_layout.h"

#include <algorithm>
#include <vector>

#include "base/numerics/safe_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "views_shell/tabs/layout_constants.h"

namespace views_shell {

namespace {
constexpr int kTabVerticalPadding = 2;
}  // namespace

UnpinnedTabContainerViewLayout::UnpinnedTabContainerViewLayout() = default;
UnpinnedTabContainerViewLayout::~UnpinnedTabContainerViewLayout() = default;

views::ProposedLayout UnpinnedTabContainerViewLayout::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layouts;

  int width = 0;
  int height = 0;

  const int horizontal_padding =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);

  for (views::View* child : host_view()->children()) {
    const int x = horizontal_padding;
    views::SizeBounds child_size_bounds =
        views::SizeBounds(size_bounds.width().is_bounded()
                              ? (size_bounds.width() - (x + horizontal_padding))
                              : size_bounds.width(),
                          size_bounds.height());
    gfx::Rect bounds = gfx::Rect(child->GetPreferredSize(child_size_bounds));
    bounds.set_x(x);

    if (!CanBeVisible(child)) {
      layouts.child_layouts.emplace_back(
          child, false, gfx::Rect(x, height, bounds.width(), 0));
      continue;
    }

    bounds.set_y(height);

    if (size_bounds.width().is_bounded()) {
      bounds.set_width(size_bounds.width().value() - bounds.x() -
                       horizontal_padding);
    }
    layouts.child_layouts.emplace_back(child, true, bounds);
    height += bounds.height() + kTabVerticalPadding;
    width = std::max(width, bounds.width() + bounds.x());
  }

  if (height > 0) {
    height -= kTabVerticalPadding;
  }

  layouts.host_size = gfx::Size(width, height);
  return layouts;
}

gfx::Size UnpinnedTabContainerViewLayout::GetMinimumSize(
    const views::View* host) const {
  int num_children = 0;
  for (const views::View* child : host->children()) {
    if (CanBeVisible(child)) {
      num_children++;
    }
  }

  const int min_height =
      base::ClampCeil(GetLayoutConstant(LayoutConstant::kVerticalTabHeight) *
                      std::min(1.5f, static_cast<float>(num_children))) +
      (num_children > 1 ? kTabVerticalPadding : 0);
  return gfx::Size(GetLayoutConstant(LayoutConstant::kVerticalTabMinWidth),
                   min_height);
}

}  // namespace views_shell
