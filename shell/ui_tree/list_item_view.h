// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The ui tree's listItem (schemas/ui-tree.schema.json): a row composed from
// stock Views only, a views::Button holding an ImageView (icon), a column of
// two views::Labels (label and sublabel) and an optional trailing view. It
// paints nothing itself: text and icon colours are the stock label and icon
// ids, and the selected state is a rounded background in
// kColorSysTonalContainer, so the theme's pins colour it (rule R17). Sizes and
// gaps come from the LayoutProvider and TypographyProvider in force (the style
// kit's).

#ifndef VIEWS_SHELL_UI_TREE_LIST_ITEM_VIEW_H_
#define VIEWS_SHELL_UI_TREE_LIST_ITEM_VIEW_H_

#include <memory>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/controls/button/button.h"

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace views_shell {

class ListItemView : public views::Button {
  METADATA_HEADER(ListItemView, views::Button)

 public:
  explicit ListItemView(PressedCallback callback = PressedCallback());
  ListItemView(const ListItemView&) = delete;
  ListItemView& operator=(const ListItemView&) = delete;
  ~ListItemView() override;

  // An empty model hides the icon.
  void SetIcon(const ui::ImageModel& icon);
  // The label is also the button's accessible name, unless one is set.
  void SetLabel(std::u16string_view label);
  // An empty sublabel is hidden.
  void SetSublabel(std::u16string_view sublabel);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  // Replaces the trailing view (null removes it) and returns the new one.
  views::View* SetTrailingView(std::unique_ptr<views::View> trailing);

  views::ImageView* icon_view() { return icon_; }
  views::Label* label() { return label_; }
  views::Label* sublabel() { return sublabel_; }
  views::View* trailing_view() { return trailing_; }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> sublabel_ = nullptr;
  raw_ptr<views::View> trailing_ = nullptr;
  bool selected_ = false;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_UI_TREE_LIST_ITEM_VIEW_H_
