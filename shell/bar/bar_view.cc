// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/bar/bar_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
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

  // The title is centred over the whole bar by Layout(), not by the flex
  // layout, so the left section and the clock keep their places.
  title_label_ = AddChildView(std::make_unique<views::Label>());
  title_label_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  title_label_->SetEnabledColor(kTextColorId);
  title_label_->SetBackgroundColor(kBackgroundColorId);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetVisible(false);

  clock_view_ = AddChildView(std::make_unique<ClockView>(clock));
  clock_view_->SetEnabledColor(kTextColorId);
  clock_view_->SetBackgroundColor(kBackgroundColorId);
  // The theme's ink exactly: no readability blending against the ground.
  clock_view_->SetAutoColorReadabilityEnabled(false);
}

BarView::~BarView() = default;

void BarView::SetLeftViewImpl(std::unique_ptr<views::View> view) {
  left_section_->RemoveAllChildViews();
  left_section_->SetUseDefaultFillLayout(true);
  left_section_->AddChildView(std::move(view));
  InvalidateLayout();
}

void BarView::SetTitle(std::u16string_view title) {
  if (title_label_->GetText() == title) {
    return;
  }
  title_label_->SetText(title);
  title_label_->SetVisible(!title.empty());
  InvalidateLayout();
}

void BarView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  if (!title_label_->GetVisible()) {
    return;
  }
  // Centred on the bar, clipped to the room between what the left section
  // actually shows and the clock.
  int left = left_section_->x();
  for (const views::View* child : left_section_->children()) {
    left = std::max(left, left_section_->x() + child->x() +
                              child->GetPreferredSize().width());
  }
  const int right = clock_view_->x();
  const int room = std::max(0, right - left);
  const int width = std::min(title_label_->GetPreferredSize().width(), room);
  const int x = std::clamp((this->width() - width) / 2, left,
                           std::max(left, right - width));
  title_label_->SetBounds(x, 0, width, height());
}

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
