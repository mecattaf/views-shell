// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab.cc @ 154.0.8037.92.

#include "views_shell/tabs/tab.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/compositor/clip_recorder.h"
#include "ui/compositor/compositor.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "views_shell/tabs/glow_hover_controller.h"
#include "views_shell/tabs/layout_constants.h"
#include "views_shell/tabs/tab_close_button.h"
#include "views_shell/tabs/tab_slot_controller.h"
#include "views_shell/tabs/tab_style.h"
#include "views_shell/tabs/tab_title.h"
#include "views_shell/tabs/tab_view_vertical_layout.h"

namespace views_shell {

namespace {

// When a non-pinned tab becomes a pinned tab the width of the tab animates. If
// the width of a pinned tab is at least kPinnedTabExtraWidthToRenderAsNormal
// larger than the desired pinned tab width then the tab is rendered as a normal
// tab. This is done to avoid having the title immediately disappear when
// transitioning a tab from normal to pinned tab.
constexpr int kPinnedTabExtraWidthToRenderAsNormal = 30;

// Helper functions ------------------------------------------------------------

// Returns the coordinate for an object of size `item_size` centered in a region
// of size `size`, biasing towards placing any extra space ahead of the object.
int Center(int size, int item_size) {
  int extra_space = size - item_size;
  // Integer division below truncates, thus effectively "rounding toward zero";
  // to always place extra space ahead of the object, we want to round towards
  // positive infinity, which means we need to bias the division only when the
  // size difference is positive.  (Adding one unconditionally will stack with
  // the truncation if `extra_space` is negative, resulting in off-by-one
  // errors.)
  if (extra_space > 0) {
    ++extra_space;
  }
  return extra_space / 2;
}

class TabStyleHighlightPathGenerator : public views::HighlightPathGenerator {
 public:
  explicit TabStyleHighlightPathGenerator(TabStyleViews* tab_style_views)
      : tab_style_views_(tab_style_views) {}
  TabStyleHighlightPathGenerator(const TabStyleHighlightPathGenerator&) =
      delete;
  TabStyleHighlightPathGenerator& operator=(
      const TabStyleHighlightPathGenerator&) = delete;

  // views::HighlightPathGenerator:
  SkPath GetHighlightPath(const views::View* view) override {
    return tab_style_views_->GetPath(TabStyle::PathType::kHighlight, 1.0, {});
  }

 private:
  const raw_ptr<TabStyleViews, AcrossTasksDanglingUntriaged> tab_style_views_;
};

class TabStyleViewDelegateImpl : public TabStyleViewDelegate {
 public:
  explicit TabStyleViewDelegateImpl(const Tab* tab) : tab_(tab) { CHECK(tab_); }
  ~TabStyleViewDelegateImpl() override = default;

  const views::View* GetView() const override { return tab_; }
  bool IsActive() const override { return tab_->IsActive(); }
  bool IsSelected() const override { return tab_->IsSelected(); }
  bool IsHovering() const override { return tab_->IsHovering(); }
  bool IsClosing() const override { return tab_->closing(); }
  bool IsDragging() const override { return tab_->dragging(); }
  int GetTabCount() const override { return tab_->controller()->GetTabCount(); }

  const TabStyleViewDelegate* GetAdjacentTab(bool leading) const override {
    const Tab* adjacent =
        tab_->controller()->GetAdjacentTab(tab_, leading ? -1 : 1);
    return (adjacent && adjacent->tab_style_views())
               ? adjacent->tab_style_views()->delegate()
               : nullptr;
  }

  float GetHoverAnimationValue() const override {
    return tab_->GetHoverAnimationValue();
  }

  float GetHoverOpacity() const override { return tab_->GetHoverOpacity(); }

  bool IsHoverAnimationActive() const override {
    return tab_->IsHoverAnimationActive();
  }

  bool IsGlassFrame() const override {
    return tab_->controller()->IsGlassFrame();
  }

  bool IsPinned() const override { return tab_->data().pinned; }

  bool ShouldPaintTabBackgroundColor() const override {
    return tab_->should_fill_background_tab_color();
  }

  int GetStrokeThickness() const override {
    return tab_->controller()->GetStrokeThickness();
  }

  GlowHoverController* GetHoverControllerForTesting() override {
    return const_cast<Tab*>(tab_.get())
        ->GetHoverControllerForTesting();  // IN-TEST
  }

 private:
  const raw_ptr<const Tab> tab_;
};

}  // namespace

// Tab -------------------------------------------------------------------------

Tab::Tab(TabSlotController* controller, TabStripOrientation orientation)
    : controller_(controller),
      orientation_(orientation),
      hover_controller_(gfx::Animation::ShouldRenderRichAnimation()
                            ? std::make_unique<GlowHoverController>(this)
                            : nullptr),
      title_(new TabTitle()) {
  DCHECK(controller);

  tab_style_views_ =
      TabStyleViews::Create(CreateStyleDelegate(this), orientation_);

  // So we get don't get enter/exit on children and don't prematurely stop the
  // hover.
  SetNotifyEnterExitOnChild(true);

  AddChildViewRaw(title_.get());

  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));

  // Unretained is safe here because this class outlives its close button, and
  // the controller outlives this Tab.
  close_button_ = AddChildView(std::make_unique<TabCloseButton>(
      base::BindRepeating(&Tab::CloseButtonPressed, base::Unretained(this)),
      base::BindRepeating(&TabSlotController::OnMouseEventInTab,
                          base::Unretained(controller_))));

  // This will cause calls to GetContentsBounds to return only the rectangle
  // inside the tab shape, rather than to its extents.
  UpdateInsets();

  // A vertical tab is laid out as Chrome lays out its vertical tab view.
  if (orientation_ == TabStripOrientation::kVertical) {
    SetLayoutManager(std::make_unique<TabViewVerticalLayout>());
  }

  // Enable keyboard focus.
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  views::FocusRing::Install(this);
  views::HighlightPathGenerator::Install(
      this,
      std::make_unique<TabStyleHighlightPathGenerator>(tab_style_views()));

  GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  UpdateAccessibleName();
}

Tab::~Tab() = default;

bool Tab::IsActive() const {
  return controller_->IsActiveTab(this);
}

bool Tab::GetHitTestMask(SkPath* mask) const {
  // When the window is maximized we don't want to shave off the edges or top
  // shadow of the tab, such that the user can click anywhere along the top
  // edge of the screen to select a tab. Ditto for immersive fullscreen.
  *mask = tab_style_views()->GetPath(
      TabStyle::PathType::kHitTest,
      GetWidget()->GetCompositor()->device_scale_factor(),
      {.render_units = TabStyle::RenderUnits::kDips});
  return true;
}

void Tab::Layout(PassKey) {
  if (orientation_ == TabStripOrientation::kVertical) {
    LayoutSuperclass<TabSlotView>(this);
    if (auto* focus_ring = views::FocusRing::Get(this); focus_ring) {
      focus_ring->DeprecatedLayoutImmediately();
    }
    return;
  }

  const gfx::Rect contents_rect = GetContentsBounds();

  UpdateIconVisibility();

  const int start = contents_rect.x();

  const int after_title_padding =
      GetLayoutConstant(LayoutConstant::kTabAfterTitlePadding);

  int close_x = contents_rect.right();
  if (showing_close_button_) {
    // The visible size is the button's hover shape size. The actual size
    // includes the border insets for the button.
    const int close_button_visible_size =
        GetLayoutConstant(LayoutConstant::kTabCloseButtonSize);
    const gfx::Size close_button_actual_size =
        close_button_->GetPreferredSize();

    // The close button is vertically centered in the contents_rect.
    const int top =
        contents_rect.y() +
        Center(contents_rect.height(), close_button_actual_size.height());

    // The visible part of the close button should be placed against the
    // right of the contents rect unless the tab is so small that it would
    // overflow the left side of the contents_rect, in that case it will be
    // placed in the middle of the tab.
    const int visible_left =
        std::max(close_x - close_button_visible_size,
                 Center(width(), close_button_visible_size));

    // Offset the new bounds rect by the extra padding in the close button.
    const int non_visible_left_padding =
        (close_button_actual_size.width() - close_button_visible_size) / 2;

    close_button_->SetBoundsRect(
        {gfx::Point(visible_left - non_visible_left_padding, top),
         close_button_actual_size});
    close_x = visible_left - after_title_padding;
  }
  close_button_->SetVisible(showing_close_button_);

  // Size the title to fill the remaining width and use all available height.
  bool show_title = ShouldRenderAsNormalTab();

  if (show_title) {
    const int title_left = start;
    int title_right = contents_rect.right();
    if (showing_close_button_) {
      // Allow the title to overlay the close button's empty border padding.
      title_right = close_x - after_title_padding;
    }
    const int title_width = std::max(title_right - title_left, 0);
    // The Label will automatically center the font's cap height within the
    // provided vertical space.
    const gfx::Rect title_bounds(title_left, GetLocalBounds().y(), title_width,
                                 GetLocalBounds().height());
    show_title = title_width > 0;
    title_->SetBoundsRect(title_bounds);
  }
  title_->SetVisible(show_title);

  if (auto* focus_ring = views::FocusRing::Get(this); focus_ring) {
    focus_ring->DeprecatedLayoutImmediately();
  }
}

bool Tab::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_RETURN && !IsSelected()) {
    controller_->SelectTab(this, event);
    return true;
  }
  return false;
}

bool Tab::OnKeyReleased(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_SPACE && !IsSelected()) {
    controller_->SelectTab(this, event);
    return true;
  }
  return false;
}

bool Tab::OnMousePressed(const ui::MouseEvent& event) {
  controller_->OnMouseEventInTab(this, event);

  // Allow a right click from touch to drag, which corresponds to a long click.
  if (event.IsOnlyLeftMouseButton() ||
      (event.IsOnlyRightMouseButton() && event.flags() & ui::EF_FROM_TOUCH)) {
    if (!IsSelected()) {
      controller_->SelectTab(this, event);
    }
  }
  return true;
}

bool Tab::OnMouseDragged(const ui::MouseEvent& event) {
  // Tab dragging is not ported; keep the press so the release reaches us.
  return true;
}

void Tab::OnMouseReleased(const ui::MouseEvent& event) {
  controller_->OnMouseEventInTab(this, event);

  // Close tab on middle click, but only if the button is released over the tab
  // (normal windows behavior is to discard presses of a UI element where the
  // releases happen off the element).
  if (event.IsOnlyMiddleMouseButton()) {
    if (HitTestPoint(event.location()) && controller_->CanCloseTab(this)) {
      controller_->CloseTab(this, CloseTabSource::kFromMouse);
    }
  } else if (event.IsOnlyLeftMouseButton() && !event.IsShiftDown() &&
             !event.IsControlDown()) {
    // If the tab was already selected mouse pressed doesn't change the
    // selection. Reset it now to handle the case where multiple tabs were
    // selected.
    controller_->SelectTab(this, event);
  }
}

void Tab::OnMouseMoved(const ui::MouseEvent& event) {
  controller_->OnMouseEventInTab(this, event);

  // Linux enter/leave events are sometimes flaky, so we don't want to "miss"
  // an enter event and fail to hover the tab.
  //
  // Either way, this is effectively a no-op if the tab is already in a hovered
  // state (crbug.com/40840442).
  MaybeUpdateHoverStatus(event);
}

void Tab::OnMouseEntered(const ui::MouseEvent& event) {
  MaybeUpdateHoverStatus(event);
}

void Tab::OnMouseExited(const ui::MouseEvent& event) {
  if (!mouse_hovered_) {
    return;
  }
  mouse_hovered_ = false;
  controller_->HideHover(this, TabStyle::HideHoverStyle::kGradual);
}

void Tab::OnGestureEvent(ui::GestureEvent* event) {
  switch (event->type()) {
    case ui::EventType::kGestureTapDown: {
      // TAP_DOWN is only dispatched for the first touch point.
      DCHECK_EQ(1, event->details().touch_points());
      if (!IsSelected()) {
        controller_->SelectTab(this, *event);
      }
      break;
    }

    default:
      break;
  }
  event->SetHandled();
}

gfx::Size Tab::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (orientation_ == TabStripOrientation::kVertical) {
    // TabViewVerticalLayout sizes a vertical tab.
    return TabSlotView::CalculatePreferredSize(available_size);
  }
  return gfx::Size(GetTabSizeInfo().standard_width, GetTabHeight());
}

void Tab::PaintChildren(const views::PaintInfo& info) {
  // Clip children based on the tab's fill path.  This has no effect except when
  // the tab is too narrow to completely show even one icon, at which point this
  // serves to clip the favicon.
  ui::ClipRecorder clip_recorder(info.context());
  // The paint recording scale for tabs is consistent along the x and y axis.
  const float paint_recording_scale = info.paint_recording_scale_x();

  const SkPath clip_path = tab_style_views()->GetPath(
      TabStyle::PathType::kHighlight, paint_recording_scale, {});

  clip_recorder.ClipPathWithAntiAliasing(clip_path);
  View::PaintChildren(info);
}

void Tab::OnPaint(gfx::Canvas* canvas) {
  tab_style_views()->PaintTab(canvas);
}

void Tab::AddedToWidget() {
  paint_as_active_subscription_ =
      GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &Tab::UpdateForegroundColors, base::Unretained(this)));
}

void Tab::RemovedFromWidget() {
  paint_as_active_subscription_ = {};
}

void Tab::OnThemeChanged() {
  TabSlotView::OnThemeChanged();
  UpdateForegroundColors();
}

TabSlotView::ViewType Tab::GetTabSlotViewType() const {
  return TabSlotView::ViewType::kTab;
}

TabSizeInfo Tab::GetTabSizeInfo() const {
  return {tab_style()->GetPinnedWidth(/*is_split=*/false),
          tab_style()->GetMinimumActiveWidth(/*is_split=*/false),
          tab_style()->GetMinimumInactiveWidth(),
          tab_style()->GetStandardWidth(/*is_split=*/false)};
}

void Tab::UpdateAccessibleName() {
  std::u16string name = controller_->GetAccessibleTabName(this);
  if (!name.empty()) {
    GetViewAccessibility().SetName(name);
  } else {
    // Under some conditions, `GetAccessibleTabName` returns an empty string.
    GetViewAccessibility().SetName(
        std::string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
  }
}

void Tab::SetClosing(bool closing) {
  closing_ = closing;
  ActiveStateChanged();

  if (closing && views::FocusRing::Get(this)) {
    // When closing, sometimes DCHECK fails because
    // cc::Layer::IsPropertyChangeAllowed() returns false. Deleting
    // the focus ring fixes this. TODO(collinbaker): investigate why
    // this happens.
    views::FocusRing::Remove(this);
  }
}

void Tab::SetData(TabData data) {
  if (data == data_) {
    return;
  }
  const bool did_title_change = data.title != data_.title;
  data_ = std::move(data);
  if (did_title_change) {
    title_->SetText(data_.title);
    // Chrome's hover cards replace tooltips for tabs; views-shell has no
    // hover card, so the title is the tooltip.
    SetTooltipText(data_.title);
    UpdateAccessibleName();
  }
  InvalidateLayout();
  DeprecatedLayoutImmediately();
  SchedulePaint();
}

void Tab::ActiveStateChanged() {
  InvalidateLayout();
  UpdateForegroundColors();
  DeprecatedLayoutImmediately();
}

bool Tab::IsApparentlyActive() const {
  return tab_style_views()->IsApparentlyActive();
}

void Tab::SelectedStateChanged() {
  UpdateForegroundColors();
  GetViewAccessibility().SetIsSelected(IsSelected());
}

bool Tab::IsSelected() const {
  return controller_->IsTabSelected(this);
}

void Tab::ShowHover(TabStyle::ShowHoverStyle style) {
  InvalidateLayout();
  if (hover_controller_) {
    if (style == TabStyle::ShowHoverStyle::kSubtle) {
      hover_controller_->SetSubtleOpacityScale(
          controller()->GetHoverOpacityForRadialHighlight());
    }
    hover_controller_->Show(style);
  }
  UpdateForegroundColors();
  DeprecatedLayoutImmediately();
}

void Tab::HideHover(TabStyle::HideHoverStyle style) {
  InvalidateLayout();
  if (hover_controller_) {
    hover_controller_->Hide(style);
  }
  UpdateForegroundColors();
  DeprecatedLayoutImmediately();
}

double Tab::GetHoverAnimationValue() const {
  if (!hover_controller_) {
    return IsHoverAnimationActive() ? 1.0 : 0.0;
  }
  return hover_controller_->GetAnimationValue();
}

float Tab::GetHoverOpacity() const {
  const float range_start =
      static_cast<float>(tab_style()->GetStandardWidth(/*is_split*/ false));
  constexpr float kWidthForMaxHoverOpacity = 32.0f;
  const float value_in_range = static_cast<float>(width());
  const float t = std::clamp(
      (value_in_range - range_start) / (kWidthForMaxHoverOpacity - range_start),
      0.0f, 1.0f);
  return controller()->GetHoverOpacityForTab(t * t);
}

bool Tab::IsHoverAnimationActive() const {
  return IsHovering() || (hover_controller_ && hover_controller_->ShouldDraw());
}

bool Tab::IsHovering() const {
  return mouse_hovered();
}

void Tab::UpdateInsets() {
  SetBorder(views::CreateEmptyBorder(tab_style_views()->GetContentsInsets()));
}

void Tab::UpdateIconVisibility() {
  // When a tab is less than it's minimum inactive width its implied that its
  // collapsed, but some favicon functionality can escape the bounds of the tab
  // causing artifacts. Fix this by explicitly disabling the visibility of the
  // views in the tab if the width is such that it is collapsed.
  if (width() < tab_style()->GetMinimumInactiveWidth()) {
    showing_close_button_ = false;
    return;
  }

  if (height() < GetTabHeight()) {
    return;
  }

  if (data().pinned) {
    // When the tab is pinned the close button is never shown.
    showing_close_button_ = false;
    return;
  }

  const int available_width = GetContentsBounds().width();

  const bool touch_ui = ui::TouchUiController::Get()->touch_ui();
  const bool large_enough_for_close_button =
      available_width >= (touch_ui ? kTouchMinimumContentsWidthForCloseButtons
                                   : kMinimumContentsWidthForCloseButtons);

  const bool should_show_close_button = controller_->CanCloseTab(this);

  if (IsActive()) {
    // Close button is shown on active tabs regardless of the size.
    showing_close_button_ = should_show_close_button;
  } else {
    showing_close_button_ =
        should_show_close_button && large_enough_for_close_button;
  }
}

bool Tab::ShouldRenderAsNormalTab() const {
  return !data().pinned || (width() >= (GetTabSizeInfo().pinned_tab_width +
                                        kPinnedTabExtraWidthToRenderAsNormal));
}

int Tab::GetTabHeight() const {
  return GetLayoutConstant(orientation_ == TabStripOrientation::kVertical
                               ? LayoutConstant::kVerticalTabHeight
                               : LayoutConstant::kTabHeight);
}

void Tab::UpdateForegroundColors() {
  TabStyle::TabColors colors = tab_style_views()->CalculateTargetColors();
  title_->SetEnabledColor(colors.foreground_color);
  close_button_->SetColors(colors);
  // There may be no focus ring when the tab is closing.
  if (auto* focus_ring = views::FocusRing::Get(this); focus_ring) {
    focus_ring->SetColorId(colors.focus_ring_color);
    focus_ring->SetOutsetFocusRingDisabled(true);
  }
  SchedulePaint();
}

void Tab::MaybeUpdateHoverStatus(const ui::MouseEvent& event) {
  if (mouse_hovered_ || !GetWidget()->IsMouseEventsEnabled()) {
    return;
  }

  mouse_hovered_ = true;
  controller_->ShowHover(this, TabStyle::ShowHoverStyle::kSubtle);
}

void Tab::CloseButtonPressed(const ui::Event& event) {
  const bool from_mouse = event.type() == ui::EventType::kMouseReleased &&
                          !(event.flags() & ui::EF_FROM_TOUCH);
  controller_->CloseTab(this, from_mouse ? CloseTabSource::kFromMouse
                                         : CloseTabSource::kFromTouch);
}

// static
std::unique_ptr<TabStyleViewDelegate> Tab::CreateStyleDelegate(const Tab* tab) {
  return std::make_unique<TabStyleViewDelegateImpl>(tab);
}

BEGIN_METADATA(Tab)
END_METADATA

}  // namespace views_shell
