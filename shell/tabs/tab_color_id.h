// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/color/chrome_color_id.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_COLOR_ID_H_
#define VIEWS_SHELL_TABS_TAB_COLOR_ID_H_

#include "ui/color/color_id.h"

namespace views_shell {

// The tab colour ids the ported tab reads, with Chrome's names, in a range of
// views-shell's own that starts at ui::kUiColorsEnd. views-shell links neither
// //chrome nor //components/color, so no other range starts there.
// tab_color_mixer.cc gives each id its recipe.
enum TabColorIds : ui::ColorId {
  kTabColorsStart = ui::kUiColorsEnd,

  kColorTabBackgroundActiveFrameActive = kTabColorsStart,
  kColorTabBackgroundActiveFrameInactive,
  kColorTabBackgroundInactiveFrameActive,
  kColorTabBackgroundInactiveFrameInactive,
  kColorTabBackgroundInactiveHoverFrameActive,
  kColorTabBackgroundInactiveHoverFrameInactive,
  kColorTabBackgroundSelectedFrameActive,
  kColorTabBackgroundSelectedFrameInactive,
  kColorTabBackgroundSelectedHoverFrameActive,
  kColorTabBackgroundSelectedHoverFrameInactive,
  kColorTabCloseButtonFocusRingActive,
  kColorTabCloseButtonFocusRingInactive,
  kColorTabFocusRingActive,
  kColorTabFocusRingInactive,
  kColorTabForegroundActiveFrameActive,
  kColorTabForegroundActiveFrameInactive,
  kColorTabForegroundInactiveFrameActive,
  kColorTabForegroundInactiveFrameInactive,

  kTabColorsEnd,
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_COLOR_ID_H_
