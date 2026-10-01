// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/layout_constants.cc @ 154.0.8037.92.

#include "views_shell/tabs/layout_constants.h"

#include "base/notreached.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/base/ui_base_features.h"

namespace views_shell {

int GetLayoutConstant(LayoutConstant constant) {
  const bool touch_ui = ui::TouchUiController::Get()->touch_ui();
  switch (constant) {
    case LayoutConstant::kTabAfterTitlePadding:
      return touch_ui ? 8 : 4;
    case LayoutConstant::kTabCloseButtonSize:
      return touch_ui ? 24 : (features::IsRoundedIconsEnabled() ? 14 : 16);
    case LayoutConstant::kTabHeight:
      return 34 + GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap);
    case LayoutConstant::kTabStripHeight:
      return GetLayoutConstant(LayoutConstant::kTabHeight) +
             GetLayoutConstant(LayoutConstant::kTabStripPadding);
    case LayoutConstant::kTabStripPadding:
      return 6;
    case LayoutConstant::kTabPreTitlePadding:
      return 8;
    case LayoutConstant::kTabVerticalPadding:
      return 6;
    case LayoutConstant::kTabHorizontalPadding:
      return 8;
    case LayoutConstant::kTabstripToolbarOverlap:
      return 1;
    case LayoutConstant::kVerticalTabCornerRadius:
      return 8;
    case LayoutConstant::kVerticalTabHeight:
      return 30;
    case LayoutConstant::kVerticalTabMinWidth:
      return 32;
    case LayoutConstant::kVerticalTabStripHorizontalPadding:
      return 12;
    case LayoutConstant::kVerticalTabStripUncollapsedVerticalPadding:
      return 12;
  }
  NOTREACHED();
}

}  // namespace views_shell
