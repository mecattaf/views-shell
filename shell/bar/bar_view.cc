// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/bar_view.cc

#include "agency/shell/bar_view.h"

#include <memory>
#include <utility>

#include "ui/color/color_id.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"

namespace agency {

BarView::BarView() {
  // Bar background: M3 semantic "header" surface token (verified present in
  // ui/color/color_id.h @150). ui::ColorVariant is implicitly constructed from
  // a ui::ColorId, so this is a fully themed, token-clean background.
  SetBackground(views::CreateSolidBackground(ui::kColorSysHeader));

  // Horizontal FlexLayout: main axis START, cross axis CENTER (vertically
  // centers the clock in the 36 DIP strip).
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetMainAxisAlignment(views::LayoutAlignment::kStart)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  // Left and right zones expand equally (weight 1, scale-to-zero .. unbounded)
  // so the weight-0 center zone stays optically centered. Both are empty
  // placeholders for Surface 1 (tray descoped, topbar-design.md section 3.4).
  const views::FlexSpecification expand =
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1);

  auto left = std::make_unique<views::View>();
  left->SetProperty(views::kFlexBehaviorKey, expand);
  left_zone_ = AddChildView(std::move(left));

  auto center = std::make_unique<views::View>();
  center->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetMainAxisAlignment(views::LayoutAlignment::kCenter)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  center_zone_ = AddChildView(std::move(center));

  auto right = std::make_unique<views::View>();
  right->SetProperty(views::kFlexBehaviorKey, expand);
  right_zone_ = AddChildView(std::move(right));

  // The clock label. Default typography (no raw font/size); colour is the M3
  // on-surface token. Text is set by ClockController on the first RenderNow().
  auto clock = std::make_unique<views::Label>();
  clock->SetEnabledColor(ui::kColorSysOnSurface);
  clock_label_ = center_zone_->AddChildView(std::move(clock));
}

BarView::~BarView() = default;

gfx::Size BarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // The compositor fills the bar's width (anchored Top|Left|Right); the
  // client-chosen height is kBarHeightDip. Width defers to the available bound
  // when the layout offers one, else 0 (compositor-filled).
  const int width =
      available_size.width().is_bounded() ? available_size.width().value() : 0;
  return gfx::Size(width, kBarHeightDip);
}

}  // namespace agency
