// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_H_
#define VIEWS_SHELL_TABS_TAB_H_

#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/masked_targeter_delegate.h"
#include "views_shell/tabs/tab_slot_view.h"
#include "views_shell/tabs/tab_strip_types.h"
#include "views_shell/tabs/tab_style_views.h"

namespace views_shell {

class GlowHoverController;
class TabCloseButton;
class TabSlotController;
class TabTitle;

// What a Tab displays. The subset of Chrome's tabs::TabData
// (chrome/browser/ui/tabs/tab_data.h) that a tab without WebContents has:
// no favicon, alert, thumbnail, discard, network or blocked state.
struct TabData {
  std::u16string title;
  bool pinned = false;

  bool operator==(const TabData& other) const = default;
};

///////////////////////////////////////////////////////////////////////////////
//
//  A View that renders a Tab in a TabStrip.
//
///////////////////////////////////////////////////////////////////////////////
class Tab : public views::MaskedTargeterDelegate, public TabSlotView {
  METADATA_HEADER(Tab, TabSlotView)

 public:
  // When the content's width of the tab shrinks to below this size we should
  // hide the close button on inactive tabs. Any smaller and they're too easy
  // to hit on accident.
  static constexpr int kMinimumContentsWidthForCloseButtons = 68;
  static constexpr int kTouchMinimumContentsWidthForCloseButtons = 100;

  // `orientation` picks the TabStyleViews (only kVertical is ported) and the
  // tab height. Chrome's Tab is always horizontal.
  Tab(TabSlotController* controller, TabStripOrientation orientation);
  Tab(const Tab&) = delete;
  Tab& operator=(const Tab&) = delete;
  ~Tab() override;

  bool IsActive() const;

  // views::MaskedTargeterDelegate:
  bool GetHitTestMask(SkPath* mask) const override;

  // TabSlotView:
  void Layout(PassKey) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintChildren(const views::PaintInfo& info) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;
  TabSlotView::ViewType GetTabSlotViewType() const override;
  TabSizeInfo GetTabSizeInfo() const override;
  void UpdateAccessibleName();

  TabSlotController* controller() const { return controller_; }

  // Used to set/check whether this Tab is being animated closed.
  void SetClosing(bool closing);
  bool closing() const { return closing_; }

  const TabData& data() const { return data_; }

  // Sets the data this tab displays.
  void SetData(TabData data);

  // Notifies the tab that the active state of this tab has changed.
  void ActiveStateChanged();

  // Returns whether the tab appears more like the active opacity than the
  // inactive opacity.
  bool IsApparentlyActive() const;

  // Called when the selected state changes.
  void SelectedStateChanged();

  // Returns true if the tab is selected.
  bool IsSelected() const;

  bool mouse_hovered() const { return mouse_hovered_; }

  void ShowHover(TabStyle::ShowHoverStyle style);
  void HideHover(TabStyle::HideHoverStyle style);

  // Returns the progress (0 to 1) of the hover animation.
  double GetHoverAnimationValue() const;
  float GetHoverOpacity() const;
  bool IsHoverAnimationActive() const;
  bool IsHovering() const;

  GlowHoverController* GetHoverControllerForTesting() {
    return hover_controller_.get();
  }

  // Returns the TabStyle associated with this tab.
  TabStyleViews* tab_style_views() { return tab_style_views_.get(); }
  const TabStyleViews* tab_style_views() const {
    return tab_style_views_.get();
  }
  const TabStyle* tab_style() const { return tab_style_views_->tab_style(); }
  bool should_fill_background_tab_color() const {
    return should_fill_background_tab_color_;
  }

  bool showing_close_button() const { return showing_close_button_; }

  raw_ptr<TabCloseButton> close_button() { return close_button_; }

  void UpdateInsets();

 private:
  // Computes which icons are visible in the tab. Should be called everytime
  // before layout is performed.
  void UpdateIconVisibility();

  // Returns whether the tab should be rendered as a normal tab as opposed to a
  // pinned tab.
  bool ShouldRenderAsNormalTab() const;

  // The height this orientation lays a tab out at.
  int GetTabHeight() const;

  // Selects, generates, and applies colors for various foreground elements to
  // ensure proper contrast. Elements affected include title text and close
  // button.
  void UpdateForegroundColors();

  // Considers switching to hovered mode based on the mouse moving over the
  // tab. If the tab is already hovered or mouse events are disabled because of
  // touch input, this is a no-op.
  void MaybeUpdateHoverStatus(const ui::MouseEvent& event);

  void CloseButtonPressed(const ui::Event& event);

  static std::unique_ptr<TabStyleViewDelegate> CreateStyleDelegate(
      const Tab* tab);

  // The controller, never nullptr.
  const raw_ptr<TabSlotController> controller_;

  const TabStripOrientation orientation_;

  TabData data_;

  std::unique_ptr<TabStyleViews> tab_style_views_;

  std::unique_ptr<GlowHoverController> hover_controller_;

  // True if the tab is being animated closed.
  bool closing_ = false;

  raw_ptr<TabCloseButton> close_button_ = nullptr;

  raw_ptr<TabTitle> title_;

  // Whether we are showing the close button. It is cached so that we can
  // detect when it changes and layout appropriately.
  bool showing_close_button_ = false;

  // Indicates whether the mouse is currently hovered over the tab. This is
  // different from View::IsMouseHovered() which does a naive intersection with
  // the view bounds.
  bool mouse_hovered_ = false;

  bool should_fill_background_tab_color_ = false;

  base::CallbackListSubscription paint_as_active_subscription_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_H_
