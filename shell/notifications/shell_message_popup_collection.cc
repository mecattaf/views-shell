// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/notifications/shell_message_popup_collection.h"

#include <algorithm>

#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/message_center/views/message_popup_view.h"

namespace views_shell::notifications {

bool PopupSurfaceSpec::operator==(const PopupSurfaceSpec&) const = default;

PopupSurfaceSpec PopupSurfaceSpecForBounds(const gfx::Rect& popup_bounds,
                                           const gfx::Rect& work_area) {
  PopupSurfaceSpec spec;
  spec.margin_top = std::max(0, popup_bounds.y() - work_area.y());
  spec.margin_right = std::max(0, work_area.right() - popup_bounds.right());
  spec.size = popup_bounds.size();
  return spec;
}

ui::LayerShellProperties ToLayerShellProperties(const PopupSurfaceSpec& spec) {
  ui::LayerShellProperties properties;
  properties.layer = spec.layer;
  properties.anchor = spec.anchor;
  properties.exclusive_zone = spec.exclusive_zone;
  properties.keyboard_interactivity = spec.keyboard;
  properties.margin_top = spec.margin_top;
  properties.margin_right = spec.margin_right;
  properties.margin_bottom = spec.margin_bottom;
  properties.margin_left = spec.margin_left;
  properties.layer_namespace = spec.layer_namespace;
  return properties;
}

ShellMessagePopupCollection::ShellMessagePopupCollection() = default;

ShellMessagePopupCollection::~ShellMessagePopupCollection() = default;

void ShellMessagePopupCollection::StartObserving() {
  display::Screen* screen = display::Screen::Get();
  if (screen_ || !screen) {
    return;
  }
  screen_ = screen;
  display_observer_.emplace(this);
  const display::Display display = screen_->GetPrimaryDisplay();
  primary_display_id_ = display.id();
  RecomputeAlignment(display);
}

bool ShellMessagePopupCollection::RecomputeAlignment(
    const display::Display& display) {
  // Alignment is fixed (top right); only the work area moves. On Wayland the
  // work area is the output's bounds: the compositor itself keeps an
  // anchored layer surface clear of other surfaces' exclusive zones (the
  // bar), so the margins stay relative to the usable area.
  if (work_area_ == display.work_area()) {
    return false;
  }
  work_area_ = display.work_area();
  return true;
}

gfx::Rect ShellMessagePopupCollection::NextPopupBounds(int popup_height) {
  const int width = message_center::GetNotificationWidth();
  int y = GetBaseline();
  for (const PopupItem& item : popup_items()) {
    y = std::max(y, item.bounds.bottom() + message_center::kMarginBetweenPopups);
  }
  const gfx::Rect size_only(0, 0, width, popup_height);
  return gfx::Rect(GetPopupOriginX(size_only), y, width, popup_height);
}

void ShellMessagePopupCollection::ConfigureWidgetInitParamsForContainer(
    views::Widget* widget,
    views::Widget::InitParams* init_params) {
  // The popup being shown is the Widget's delegate (MessagePopupView::Show).
  auto* popup = static_cast<message_center::MessagePopupView*>(
      init_params->delegate.get());
  const int height =
      popup ? popup->GetCachedHeightForWidth(
                  message_center::GetNotificationWidth())
            : 0;
  const gfx::Rect bounds = NextPopupBounds(std::max(1, height));
  init_params->bounds = bounds;

  PopupSurfaceSpec spec = PopupSurfaceSpecForBounds(bounds, work_area_);
  surface_specs_.push_back(spec);
  // The popup is one zwlr_layer_surface_v1 (rule R1): DesktopWindowTreeHostLinux
  // turns this into PlatformWindowType::kLayerShell.
  init_params->layer_shell = ToLayerShellProperties(spec);
  if (widget_context_for_testing_) {
    init_params->context = widget_context_for_testing_;
  }
}

int ShellMessagePopupCollection::GetPopupOriginX(
    const gfx::Rect& popup_bounds) const {
  return work_area_.right() - message_center::kMarginBetweenPopups -
         popup_bounds.width();
}

int ShellMessagePopupCollection::GetBaseline() const {
  return work_area_.y() + message_center::kMarginBetweenPopups;
}

gfx::Rect ShellMessagePopupCollection::GetWorkArea() const {
  return work_area_;
}

bool ShellMessagePopupCollection::IsTopDown() const {
  return true;
}

bool ShellMessagePopupCollection::IsFromLeft() const {
  return false;
}

bool ShellMessagePopupCollection::IsPrimaryDisplayForNotification() const {
  return true;
}

bool ShellMessagePopupCollection::BlockForMixedFullscreen(
    const message_center::Notification& notification) const {
  // Fullscreen belongs to the compositor's windows (rule R3); the overlay
  // layer already decides whether a popup shows above them.
  return false;
}

bool ShellMessagePopupCollection::CanUseTransformForBoundsAnimation() const {
  // Each popup is its own surface; a transform cannot move it.
  return false;
}

void ShellMessagePopupCollection::UpdatePrimaryDisplay() {
  const display::Display primary = screen_->GetPrimaryDisplay();
  if (primary.id() != primary_display_id_) {
    primary_display_id_ = primary.id();
    if (RecomputeAlignment(primary)) {
      ResetBounds();
    }
  }
}

void ShellMessagePopupCollection::OnDisplayAdded(
    const display::Display& new_display) {
  UpdatePrimaryDisplay();
}

void ShellMessagePopupCollection::OnDisplaysRemoved(
    const display::Displays& removed_displays) {
  UpdatePrimaryDisplay();
}

void ShellMessagePopupCollection::OnDisplayMetricsChanged(
    const display::Display& display,
    uint32_t changed_metrics) {
  primary_display_id_ = display::kInvalidDisplayId;
  UpdatePrimaryDisplay();
}

}  // namespace views_shell::notifications
