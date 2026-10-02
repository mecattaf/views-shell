// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/ui_tree/list_item_view.h"

#include <string>
#include <utility>

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"

namespace views_shell {

ListItemView::ListItemView(PressedCallback callback)
    : views::Button(std::move(callback)) {
  const views::LayoutProvider* provider = views::LayoutProvider::Get();
  const int gap =
      provider->GetDistanceMetric(views::DISTANCE_RELATED_LABEL_HORIZONTAL);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      provider->GetInsetsMetric(views::INSETS_LABEL_BUTTON), gap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  icon_ = AddChildView(std::make_unique<views::ImageView>());
  icon_->SetVisible(false);
  icon_->SetCanProcessEventsWithinSubtree(false);

  auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
  text->SetOrientation(views::BoxLayout::Orientation::kVertical);
  text->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  text->SetCanProcessEventsWithinSubtree(false);
  layout->SetFlexForView(text, 1);

  label_ = text->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3));
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  sublabel_ = text->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  sublabel_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  sublabel_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  sublabel_->SetVisible(false);
}

ListItemView::~ListItemView() = default;

void ListItemView::SetIcon(const ui::ImageModel& icon) {
  icon_->SetImage(icon);
  icon_->SetVisible(!icon.IsEmpty());
}

void ListItemView::SetLabel(std::u16string_view label) {
  label_->SetText(label);
  if (!label.empty()) {
    GetViewAccessibility().SetName(std::u16string(label));
  }
}

void ListItemView::SetSublabel(std::u16string_view sublabel) {
  sublabel_->SetText(sublabel);
  sublabel_->SetVisible(!sublabel.empty());
}

void ListItemView::SetSelected(bool selected) {
  if (selected == selected_ && (background() != nullptr) == selected) {
    return;
  }
  selected_ = selected;
  if (selected) {
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysTonalContainer,
        views::LayoutProvider::Get()->GetCornerRadiusMetric(
            views::Emphasis::kMedium)));
  } else {
    SetBackground(nullptr);
  }
  GetViewAccessibility().SetIsSelected(selected);
}

views::View* ListItemView::SetTrailingView(
    std::unique_ptr<views::View> trailing) {
  if (trailing_) {
    views::View* old = trailing_;
    trailing_ = nullptr;
    RemoveChildViewT(old);
  }
  if (trailing) {
    trailing_ = AddChildView(std::move(trailing));
  }
  return trailing_;
}

BEGIN_METADATA(ListItemView)
END_METADATA

}  // namespace views_shell
