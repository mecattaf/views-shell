// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/vertical_tab_style_views.cc @ 154.0.8037.92.

#include "views_shell/tabs/vertical_tab_style_views.h"

#include <utility>

#include "base/check.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "views_shell/tabs/layout_constants.h"

namespace views_shell {

VerticalTabStyleViews::VerticalTabStyleViews(
    std::unique_ptr<TabStyleViewDelegate> delegate)
    : delegate_(std::move(delegate)) {
  CHECK(delegate_);
}

VerticalTabStyleViews::~VerticalTabStyleViews() = default;

const TabStyleViewDelegate* VerticalTabStyleViews::delegate() const {
  return delegate_.get();
}

double VerticalTabStyleViews::GetHoverAnimationValue() const {
  return delegate_->GetHoverAnimationValue();
}

GlowHoverController* VerticalTabStyleViews::GetHoverControllerForTesting() {
  return delegate_->GetHoverControllerForTesting();  // IN-TEST
}

SkPath VerticalTabStyleViews::GetPath(TabStyle::PathType path_type,
                                      float scale,
                                      const TabPathFlags& flags) const {
  const SkScalar corner_radius = GetCornerRadius();
  gfx::RectF bounds(delegate_->GetView()->GetLocalBounds());

  if (flags.render_units == TabStyle::RenderUnits::kPixels) {
    bounds.Scale(scale);
  }
  const SkScalar scaled_corner_radius =
      flags.render_units == TabStyle::RenderUnits::kPixels
          ? corner_radius * scale
          : corner_radius;

  return SkPath::RRect(SkRRect::MakeRectXY(
      gfx::RectFToSkRect(bounds), scaled_corner_radius, scaled_corner_radius));
}

void VerticalTabStyleViews::PaintTab(gfx::Canvas* canvas) const {
  // Theme images (IDR_THEME_TOOLBAR and the frame's custom background) are
  // Chrome theme-service state; views-shell paints the colour fill only.
  PaintTabBackgroundFill(canvas, GetSelectionState(),
                         delegate_->IsHoverAnimationActive());
}

void VerticalTabStyleViews::PaintTabBackgroundFill(
    gfx::Canvas* canvas,
    TabStyle::TabSelectionState selection_state,
    bool hovered) const {
  const SkPath fill_path =
      GetPath(TabStyle::PathType::kHighlight, canvas->image_scale(), {});

  gfx::ScopedCanvas scoped_canvas(canvas);
  const float scale = canvas->UndoDeviceScaleFactor();

  canvas->ClipPath(fill_path, true);

  if (ShouldPaintTabBackgroundColor(selection_state, hovered)) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetCurrentTabBackgroundColor(selection_state));
    canvas->DrawRect(gfx::ScaleToEnclosingRect(
                         delegate_->GetView()->GetLocalBounds(), scale),
                     flags);
  }
}

bool VerticalTabStyleViews::ShouldPaintTabBackgroundColor(
    TabStyle::TabSelectionState selection_state,
    bool hovered) const {
  if (selection_state == TabStyle::TabSelectionState::kActive ||
      selection_state == TabStyle::TabSelectionState::kSelected) {
    return true;
  }

  if (hovered) {
    return true;
  }

  return delegate_->ShouldPaintTabBackgroundColor();
}

SkColor VerticalTabStyleViews::GetCurrentTabBackgroundColor(
    TabStyle::TabSelectionState selection_state) const {
  const bool frame_glass = delegate_->IsGlassFrame();
  return tab_style()->GetCurrentTabBackgroundColor(
      selection_state, delegate_->IsHoverAnimationActive(),
      delegate_->GetHoverAnimationValue(),
      delegate_->GetView()->GetWidget()
          ? delegate_->GetView()->GetWidget()->ShouldPaintAsActive()
          : true,
      frame_glass, delegate_->GetView()->GetColorProvider());
}

TabStyle::TabSelectionState VerticalTabStyleViews::GetSelectionState() const {
  if (delegate_->IsActive()) {
    return TabStyle::TabSelectionState::kActive;
  }

  if (delegate_->IsSelected()) {
    return TabStyle::TabSelectionState::kSelected;
  }

  return TabStyle::TabSelectionState::kInactive;
}

SkScalar VerticalTabStyleViews::GetCornerRadius() const {
  return SkIntToScalar(
      GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius));
}

gfx::Insets VerticalTabStyleViews::GetContentsInsets() const {
  return gfx::Insets::VH(
      GetLayoutConstant(LayoutConstant::kTabVerticalPadding),
      GetLayoutConstant(LayoutConstant::kTabHorizontalPadding));
}

int VerticalTabStyleViews::GetStrokeThickness() const {
  return delegate_->GetStrokeThickness();
}

SkPath VerticalTabStyleViews::GetOverlinePath(float scale) const {
  return SkPath();
}

bool VerticalTabStyleViews::IsApparentlyActive() const {
  const TabStyle::TabSelectionState selection_state = GetSelectionState();
  if (selection_state == TabStyle::TabSelectionState::kActive) {
    return true;
  }
  if (delegate_->IsHovering()) {
    return delegate_->GetHoverOpacity() > 0.5f;
  }
  return selection_state == TabStyle::TabSelectionState::kSelected;
}

TabStyle::TabColors VerticalTabStyleViews::CalculateTargetColors() const {
  return tab_style()->CalculateTargetColors(
      GetSelectionState(), IsApparentlyActive(), delegate_->IsHovering(),
      delegate_->GetView()->GetWidget()
          ? delegate_->GetView()->GetWidget()->ShouldPaintAsActive()
          : true,
      delegate_->GetView()->GetColorProvider());
}

}  // namespace views_shell
