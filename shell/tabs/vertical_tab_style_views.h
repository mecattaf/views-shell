// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/vertical_tab_style_views.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_VERTICAL_TAB_STYLE_VIEWS_H_
#define VIEWS_SHELL_TABS_VERTICAL_TAB_STYLE_VIEWS_H_

#include <memory>

#include "third_party/skia/include/core/SkScalar.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "views_shell/tabs/tab_style.h"
#include "views_shell/tabs/tab_style_views.h"

namespace views_shell {

class VerticalTabStyleViews : public TabStyleViews {
 public:
  explicit VerticalTabStyleViews(
      std::unique_ptr<TabStyleViewDelegate> delegate);
  VerticalTabStyleViews(const VerticalTabStyleViews&) = delete;
  VerticalTabStyleViews& operator=(const VerticalTabStyleViews&) = delete;
  ~VerticalTabStyleViews() override;

  // TabStyleViews:
  SkPath GetPath(TabStyle::PathType path_type,
                 float scale,
                 const TabPathFlags& flags) const override;
  void PaintTab(gfx::Canvas* canvas) const override;

  gfx::Insets GetContentsInsets() const override;
  int GetStrokeThickness() const override;
  SkPath GetOverlinePath(float scale) const override;

  bool IsApparentlyActive() const override;
  TabStyle::TabColors CalculateTargetColors() const override;
  const TabStyleViewDelegate* delegate() const override;
  double GetHoverAnimationValue() const override;
  GlowHoverController* GetHoverControllerForTesting() override;

 private:
  SkScalar GetCornerRadius() const;
  void PaintTabBackgroundFill(gfx::Canvas* canvas,
                              TabStyle::TabSelectionState selection_state,
                              bool hovered) const;
  bool ShouldPaintTabBackgroundColor(
      TabStyle::TabSelectionState selection_state,
      bool hovered) const;
  SkColor GetCurrentTabBackgroundColor(
      TabStyle::TabSelectionState selection_state) const;
  TabStyle::TabSelectionState GetSelectionState() const;

  std::unique_ptr<TabStyleViewDelegate> delegate_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_VERTICAL_TAB_STYLE_VIEWS_H_
