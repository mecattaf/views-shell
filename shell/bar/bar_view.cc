// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/bar/bar_view.h"

#include <memory>
#include <utility>

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view_class_properties.h"
#include "views_shell/bar/clock_view.h"

namespace views_shell {

BarView::BarView(const base::Clock* clock) {
  SetBackground(views::CreateSolidBackground(kBackgroundColorId));

  // The side padding follows the kit's spacing (style/layout_provider.h).
  const int padding = views::LayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_HORIZONTAL);
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets::VH(0, padding));

  left_section_ = AddChildView(std::make_unique<views::View>());
  left_section_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  clock_view_ = AddChildView(std::make_unique<ClockView>(clock));
  clock_view_->SetEnabledColor(kTextColorId);
  clock_view_->SetBackgroundColor(kBackgroundColorId);
  // The theme's ink exactly: no readability blending against the ground.
  clock_view_->SetAutoColorReadabilityEnabled(false);
}

BarView::~BarView() = default;

void BarView::SetFirstPaintCallback(base::OnceClosure callback) {
  on_first_paint_ = std::move(callback);
}

void BarView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (on_first_paint_) {
    std::move(on_first_paint_).Run();
  }
}

BEGIN_METADATA(BarView)
END_METADATA

}  // namespace views_shell
