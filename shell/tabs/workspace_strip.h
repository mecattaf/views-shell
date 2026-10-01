// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A vertical strip of Chrome tabs, one per workspace. The Tab, its
// VerticalTabStyleViews and its close button are ported from Chrome
// (CHROME-PORT-LEDGER.md); this view is views-shell's own. It plays the part
// Chrome's TabStrip plays for a Tab (it is the TabSlotController) and lays the
// tabs out top to bottom with the metrics of Chrome's vertical tab strip.

#ifndef VIEWS_SHELL_TABS_WORKSPACE_STRIP_H_
#define VIEWS_SHELL_TABS_WORKSPACE_STRIP_H_

#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "views_shell/tabs/tab_slot_controller.h"
#include "views_shell/tabs/workspace_source.h"

namespace views_shell {

class Tab;

class WorkspaceStrip : public views::View, public TabSlotController {
  METADATA_HEADER(WorkspaceStrip, views::View)

 public:
  // The strip's width when it is the left edge of a window.
  static constexpr int kPreferredWidth = 220;

  // Runs when a click, tap or key selects a workspace other than the active
  // one. For now the strip only redraws; asking the compositor to focus the
  // workspace is the caller's business.
  using SelectCallback = base::RepeatingCallback<void(const Workspace&)>;

  explicit WorkspaceStrip(SelectCallback on_select);
  WorkspaceStrip(const WorkspaceStrip&) = delete;
  WorkspaceStrip& operator=(const WorkspaceStrip&) = delete;
  ~WorkspaceStrip() override;

  // Replaces every tab with one per workspace, in order.
  void SetWorkspaces(std::vector<Workspace> workspaces);

  // The active workspace's index, or -1 when there is none.
  int active_index() const { return active_index_; }
  const std::vector<Workspace>& workspaces() const { return workspaces_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

  // TabSlotController:
  void SelectTab(Tab* tab, const ui::Event& event) override;
  void CloseTab(Tab* tab, CloseTabSource source) override;
  bool CanCloseTab(const Tab* tab) const override;
  int GetTabCount() const override;
  bool IsActiveTab(const TabSlotView* tab) const override;
  bool IsTabSelected(const TabSlotView* tab) const override;
  Tab* GetAdjacentTab(const Tab* tab, int offset) override;
  void OnMouseEventInTab(views::View* source,
                         const ui::MouseEvent& event) override;
  void ShowHover(Tab* tab, TabStyle::ShowHoverStyle style) override;
  void HideHover(Tab* tab, TabStyle::HideHoverStyle style) override;
  int GetStrokeThickness() const override;
  bool IsGlassFrame() const override;
  std::u16string GetAccessibleTabName(const Tab* tab) const override;
  float GetHoverOpacityForTab(float range_parameter) const override;
  float GetHoverOpacityForRadialHighlight() const override;

 private:
  int IndexOf(const TabSlotView* tab) const;

  // As TabStrip::UpdateContrastRatioValues: the hover opacities follow the
  // colour provider and the window's active state.
  void UpdateContrastRatioValues();

  SelectCallback on_select_;
  std::vector<Workspace> workspaces_;
  std::vector<raw_ptr<Tab>> tabs_;
  int active_index_ = -1;

  float hover_opacity_min_ = 1.0f;
  float hover_opacity_max_ = 1.0f;
  float radial_highlight_opacity_ = 1.0f;

  base::CallbackListSubscription paint_as_active_subscription_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_WORKSPACE_STRIP_H_
