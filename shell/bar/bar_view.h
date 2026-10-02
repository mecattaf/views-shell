// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The bar: the contents of the "bar" surface (app/surface_spec.cc). A
// horizontal views::FlexLayout row with two sections: the left one takes the
// free width and holds what the program puts there (the workspace strip,
// bar/workspace_strip.h; plugin slots later); the right one is the ClockView.
// The title of the focused window sits centred over the whole bar, between
// the two, outside the flex layout (SetTitle; empty by default). Every colour is a ui::ColorProvider id, so the
// bar wears whatever theme the style kit applied (rule R17): the ground is
// kColorSysBase and the ink kColorSysOnSurface, which the theme pins to its
// background and foreground (style/theme-map.json). Without a theme they are
// the stock ui/color renditions.

#ifndef VIEWS_SHELL_BAR_BAR_VIEW_H_
#define VIEWS_SHELL_BAR_BAR_VIEW_H_

#include <memory>
#include <string_view>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/clock.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace views_shell {

class ClockView;

class BarView : public views::View {
  METADATA_HEADER(BarView, views::View)

 public:
  static constexpr ui::ColorId kBackgroundColorId = ui::kColorSysBase;
  static constexpr ui::ColorId kTextColorId = ui::kColorSysOnSurface;

  // `clock` drives the ClockView and must outlive the bar.
  explicit BarView(const base::Clock* clock);
  BarView(const BarView&) = delete;
  BarView& operator=(const BarView&) = delete;
  ~BarView() override;

  views::View* left_section() { return left_section_; }
  ClockView* clock_view() { return clock_view_; }
  views::Label* title_label() { return title_label_; }

  // Puts `view` (the workspace strip) into the left section, filling it.
  template <typename T>
  T* SetLeftView(std::unique_ptr<T> view) {
    T* raw = view.get();
    SetLeftViewImpl(std::move(view));
    return raw;
  }

  // The centred title (the focused window's title); empty hides it.
  void SetTitle(std::u16string_view title);

  // Runs `callback` once, after the bar's first paint (the --demo-popup menu
  // waits for it).
  void SetFirstPaintCallback(base::OnceClosure callback);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;

 private:
  void SetLeftViewImpl(std::unique_ptr<views::View> view);

  raw_ptr<views::View> left_section_ = nullptr;
  raw_ptr<ClockView> clock_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  base::OnceClosure on_first_paint_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_BAR_BAR_VIEW_H_
