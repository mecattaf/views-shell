// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.cc @ 154.0.8037.92.

#include "views_shell/tabs/tab_view_vertical_layout.h"

#include "base/check.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "views_shell/tabs/layout_constants.h"
#include "views_shell/tabs/tab.h"
#include "views_shell/tabs/tab_close_button.h"
#include "views_shell/tabs/tab_slot_controller.h"
#include "views_shell/tabs/tab_style_views.h"
#include "views_shell/tabs/tab_title.h"

namespace views_shell {

namespace {
constexpr int kIconDesignWidth = 16;
constexpr int kTitleMinWidth = 10;
constexpr int kDefaultPadding = 4;

// From chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h,
// chrome/browser/ui/views/tabs/common/split_tab_view.h and tab_group_view.h.
constexpr int kUncollapsedMinWidth = 126;
constexpr int kSplitViewGap = 2;
constexpr int kTabLeadingPadding = 10;
}  // namespace

TabViewVerticalLayout::TabViewVerticalLayout() = default;
TabViewVerticalLayout::~TabViewVerticalLayout() = default;

// static
int TabViewVerticalLayout::UncollapsedMinWidth() {
  return (kUncollapsedMinWidth -
          2 * GetLayoutConstant(
                  LayoutConstant::kVerticalTabStripHorizontalPadding) -
          kSplitViewGap - kTabLeadingPadding) /
         2;
}

const Tab& TabViewVerticalLayout::tab() const {
  return static_cast<const Tab&>(*host_view());
}

void TabViewVerticalLayout::OnInstalled(views::View* host) {
  views::LayoutManagerBase::OnInstalled(host);
  // Chrome's order is close button, alert indicator, icon, title. The alert
  // indicator and the icon are not ported (CHROME-PORT-LEDGER.md).
  tab_children_configs_ = {
      TabChildConfig(tab().close_button_.get(), kIconDesignWidth,
                     kDefaultPadding,
                     /*align_leading=*/false,
                     /*expand=*/false),
      TabChildConfig(tab().title_.get(), kTitleMinWidth, kDefaultPadding,
                     /*align_leading=*/true,
                     /*expand=*/true)};
}

views::ProposedLayout TabViewVerticalLayout::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  const int width = size_bounds.width().value_or(kUncollapsedMaxWidth);
  const int height = GetLayoutConstant(LayoutConstant::kVerticalTabHeight);
  views::ProposedLayout layouts;
  layouts.host_size = gfx::Size(width, height);

  gfx::Rect bounds_remaining = gfx::Rect(layouts.host_size);
  bounds_remaining.Inset(tab().tab_style_views()->GetContentsInsets());

  // Pinned tabs center their one child. The strip never collapses, so
  // Chrome's collapsed and expand-on-hover states do not arise.
  const bool is_centered = tab().data().pinned;

  int placed_children = 0;
  for (const auto& child : tab_children_configs_) {
    const bool can_render_child =
        is_centered
            ? (placed_children == 0)
            : (child.min_width + child.padding < bounds_remaining.width() ||
               placed_children < 2);
    const bool is_child_visible = IsChildVisible(child.view, width);
    if (is_child_visible && can_render_child) {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          GetChildBounds(bounds_remaining, child, is_centered));

      if (!is_centered) {
        bounds_remaining.Inset(
            child.align_leading
                ? gfx::Insets().set_left(child.padding + child.min_width)
                : gfx::Insets().set_right(child.padding + child.min_width));
      }

      placed_children += 1;
    } else {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          gfx::Rect(bounds_remaining.x(), bounds_remaining.y(), 0, 0));
    }
  }

  return layouts;
}

gfx::Rect TabViewVerticalLayout::GetChildBounds(const gfx::Rect& container,
                                                const TabChildConfig& config,
                                                const bool center) const {
  int preferred_width;
  int preferred_height;
  if (config.expand) {
    preferred_width = container.width() - config.padding;
    // The only expandable view is the views::Label. Just get the line height to
    // make calculating bounds cheaper.
    views::Label* label = views::AsViewClass<views::Label>(config.view);
    CHECK(label);
    preferred_height = label->GetLineHeight();
  } else {
    const gfx::Size preferred_size = config.view->GetPreferredSize();
    preferred_width = preferred_size.width();
    preferred_height = preferred_size.height();
  }

  // Some icons have larger sizes to account for decoration. Make a distinction
  // between the design width and the actual width.
  const int design_width =
      config.expand ? container.width() - config.padding : config.min_width;

  int x = container.x();
  if (center) {
    x += 0.5 * (container.width() - preferred_width);
  } else if (config.align_leading) {
    x += 0.5 * (design_width - preferred_width);
  } else {
    x += container.width() - 0.5 * (design_width + preferred_width);
  }
  const int y = container.y() + 0.5 * (container.height() - preferred_height);

  return gfx::Rect(x, y, preferred_width, preferred_height);
}

bool TabViewVerticalLayout::IsChildVisible(const views::View* child_view,
                                           const int width) const {
  if (child_view == tab().title_.get()) {
    return !tab().data().pinned;
  }

  CHECK(child_view == tab().close_button_.get());
  if (tab().data().pinned) {
    return false;
  }
  // Replaces Chrome's ChromeOS OnTask lock: the controller decides whether
  // this tab can be closed at all.
  if (!tab().controller()->CanCloseTab(&tab())) {
    return false;
  }

  // When uncollapsing the tabstrip, intentionally start showing the close
  // button on active non-hovered tabs a little bit sooner than reaching the
  // uncollapsed min width, because otherwise the close buttons in a grouped
  // split tab will visibly show up at different times due to rounding.
  constexpr int kUncollapsedMinWidthThreshold = 3;

  const bool hovered_or_focused =
      tab().IsHovering() || tab().HasFocus() ||
      (tab().close_button_ && tab().close_button_->HasFocus());
  if (width < UncollapsedMinWidth() - kUncollapsedMinWidthThreshold) {
    return tab().IsActive() && hovered_or_focused;
  }

  return tab().IsActive() || hovered_or_focused;
}

}  // namespace views_shell
