// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/tabs/workspace_strip.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "views_shell/tabs/layout_constants.h"
#include "views_shell/tabs/tab.h"
#include "views_shell/tabs/tab_color_id.h"
#include "views_shell/tabs/unpinned_tab_container_view_layout.h"

namespace views_shell {
WorkspaceStrip::WorkspaceStrip(SelectCallback on_select)
    : on_select_(std::move(on_select)) {
  // As VerticalTabStripRegionView: uncollapsed vertical padding above and
  // below the tabs. Chrome's top padding comes from its top button container,
  // which the strip does not have.
  const int vertical_padding = GetLayoutConstant(
      LayoutConstant::kVerticalTabStripUncollapsedVerticalPadding);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(vertical_padding, 0)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  tab_container_ = AddChildView(std::make_unique<views::View>());
  tab_container_->SetLayoutManager(
      std::make_unique<UnpinnedTabContainerViewLayout>());
  GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  GetViewAccessibility().SetName(u"Workspaces");
}

WorkspaceStrip::~WorkspaceStrip() = default;

void WorkspaceStrip::SetWorkspaces(std::vector<Workspace> workspaces) {
  // The container's only children are the tabs.
  tabs_.clear();
  tab_container_->RemoveAllChildViews();

  workspaces_ = std::move(workspaces);
  active_index_ = -1;
  for (size_t i = 0; i < workspaces_.size(); ++i) {
    if (workspaces_[i].active && active_index_ < 0) {
      active_index_ = static_cast<int>(i);
    }
  }

  for (const Workspace& workspace : workspaces_) {
    Tab* tab = tab_container_->AddChildView(
        std::make_unique<Tab>(this, TabStripOrientation::kVertical));
    tab->SetData({.title = workspace.title});
    tabs_.push_back(tab);
  }
  for (Tab* tab : tabs_) {
    tab->ActiveStateChanged();
    tab->SelectedStateChanged();
  }
  PreferredSizeChanged();
  InvalidateLayout();
  SchedulePaint();
}

gfx::Size WorkspaceStrip::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // A fixed width; the height is what the box layout asks for the tabs.
  return gfx::Size(
      kPreferredWidth,
      views::View::CalculatePreferredSize(
          views::SizeBounds(kPreferredWidth, available_size.height()))
          .height());
}

void WorkspaceStrip::OnPaint(gfx::Canvas* canvas) {
  // The strip is the frame behind the tabs: inactive tabs paint nothing and
  // show it, as background tabs show the frame in Chrome.
  const bool frame_active = !GetWidget() || GetWidget()->ShouldPaintAsActive();
  canvas->DrawColor(GetColorProvider()->GetColor(
      frame_active ? kColorTabBackgroundInactiveFrameActive
                   : kColorTabBackgroundInactiveFrameInactive));
}

void WorkspaceStrip::AddedToWidget() {
  paint_as_active_subscription_ =
      GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &WorkspaceStrip::UpdateContrastRatioValues, base::Unretained(this)));
}

void WorkspaceStrip::RemovedFromWidget() {
  paint_as_active_subscription_ = {};
}

void WorkspaceStrip::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateContrastRatioValues();
}

void WorkspaceStrip::SelectTab(Tab* tab, const ui::Event& event) {
  const int index = IndexOf(tab);
  if (index < 0 || index == active_index_) {
    return;
  }
  Tab* previous = active_index_ >= 0 ? tabs_[active_index_].get() : nullptr;
  if (active_index_ >= 0) {
    workspaces_[active_index_].active = false;
  }
  active_index_ = index;
  workspaces_[index].active = true;
  for (Tab* changed : {previous, tab}) {
    if (changed) {
      changed->ActiveStateChanged();
      changed->SelectedStateChanged();
    }
  }
  if (on_select_) {
    on_select_.Run(workspaces_[index]);
  }
}

void WorkspaceStrip::CloseTab(Tab* tab, CloseTabSource source) {
  // Workspaces are not closed from the strip. CanCloseTab() is false, so the
  // close button stays hidden and a middle click does not ask.
}

bool WorkspaceStrip::CanCloseTab(const Tab* tab) const {
  return false;
}

int WorkspaceStrip::GetTabCount() const {
  return static_cast<int>(tabs_.size());
}

bool WorkspaceStrip::IsActiveTab(const TabSlotView* tab) const {
  return active_index_ >= 0 && IndexOf(tab) == active_index_;
}

bool WorkspaceStrip::IsTabSelected(const TabSlotView* tab) const {
  // One selected tab, the active one: no multi-selection.
  return IsActiveTab(tab);
}

Tab* WorkspaceStrip::GetAdjacentTab(const Tab* tab, int offset) {
  const int index = IndexOf(tab);
  if (index < 0) {
    return nullptr;
  }
  const int adjacent = index + offset;
  if (adjacent < 0 || adjacent >= GetTabCount()) {
    return nullptr;
  }
  return tabs_[adjacent];
}

void WorkspaceStrip::OnMouseEventInTab(views::View* source,
                                       const ui::MouseEvent& event) {
  // Chrome records a time-to-switch histogram here; views-shell keeps no
  // metrics.
}

void WorkspaceStrip::ShowHover(Tab* tab, TabStyle::ShowHoverStyle style) {
  tab->ShowHover(style);
}

void WorkspaceStrip::HideHover(Tab* tab, TabStyle::HideHoverStyle style) {
  tab->HideHover(style);
}

int WorkspaceStrip::GetStrokeThickness() const {
  return 0;
}

bool WorkspaceStrip::IsGlassFrame() const {
  return false;
}

std::u16string WorkspaceStrip::GetAccessibleTabName(const Tab* tab) const {
  const int index = IndexOf(tab);
  return index >= 0 ? workspaces_[index].title : std::u16string();
}

float WorkspaceStrip::GetHoverOpacityForTab(float range_parameter) const {
  return gfx::Tween::FloatValueBetween(range_parameter, hover_opacity_min_,
                                       hover_opacity_max_);
}

float WorkspaceStrip::GetHoverOpacityForRadialHighlight() const {
  return radial_highlight_opacity_;
}

int WorkspaceStrip::IndexOf(const TabSlotView* tab) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i] == tab) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void WorkspaceStrip::UpdateContrastRatioValues() {
  if (!GetWidget() || !GetColorProvider()) {
    return;
  }
  // The separator colour (the fourth value) is for horizontal tabs only.
  const auto values = TabStyle::Get()->GetContrastRatioValues(
      GetWidget()->ShouldPaintAsActive(), GetColorProvider());
  hover_opacity_min_ = std::get<0>(values);
  hover_opacity_max_ = std::get<1>(values);
  radial_highlight_opacity_ = std::get<2>(values);
  SchedulePaint();
}

BEGIN_METADATA(WorkspaceStrip)
END_METADATA

}  // namespace views_shell
