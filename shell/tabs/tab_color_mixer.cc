// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/color/material_tab_strip_color_mixer.cc @ 154.0.8037.92.

#include "views_shell/tabs/tab_color_mixer.h"

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/gfx/color_utils.h"
#include "views_shell/tabs/tab_color_id.h"

namespace views_shell {

void AddTabColorMixer(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  mixer[kColorTabBackgroundActiveFrameActive] = {ui::kColorSysBase};
  mixer[kColorTabBackgroundActiveFrameInactive] = {
      kColorTabBackgroundActiveFrameActive};

  mixer[kColorTabBackgroundInactiveFrameActive] = {ui::kColorSysHeader};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {
      ui::kColorSysHeaderInactive};
  mixer[kColorTabBackgroundInactiveHoverFrameActive] = {
      ui::kColorSysStateHeaderHover};
  mixer[kColorTabBackgroundInactiveHoverFrameInactive] = {
      ui::kColorSysStateHeaderHoverInactive};

  mixer[kColorTabBackgroundSelectedFrameActive] = {ui::GetResultingPaintColor(
      ui::kColorSysStateHeaderSelect, kColorTabBackgroundInactiveFrameActive)};
  mixer[kColorTabBackgroundSelectedFrameInactive] = {
      ui::GetResultingPaintColor(ui::kColorSysStateHeaderSelect,
                                 kColorTabBackgroundInactiveFrameInactive)};
  mixer[kColorTabBackgroundSelectedHoverFrameActive] = {
      ui::GetResultingPaintColor(ui::kColorSysStateHoverDimBlendProtection,
                                 kColorTabBackgroundSelectedFrameActive)};
  mixer[kColorTabBackgroundSelectedHoverFrameInactive] = {
      ui::GetResultingPaintColor(ui::kColorSysStateHoverDimBlendProtection,
                                 kColorTabBackgroundSelectedFrameInactive)};

  mixer[kColorTabForegroundActiveFrameActive] = {ui::kColorSysOnSurface};
  mixer[kColorTabForegroundActiveFrameInactive] = {
      kColorTabForegroundActiveFrameActive};
  mixer[kColorTabForegroundInactiveFrameActive] =
      ui::BlendForMinContrast({ui::kColorSysOnSurfaceSecondary},
                              {kColorTabBackgroundInactiveFrameActive});
  mixer[kColorTabForegroundInactiveFrameInactive] =
      ui::BlendForMinContrast({kColorTabForegroundInactiveFrameActive},
                              {kColorTabBackgroundInactiveFrameInactive});

  // From chrome/browser/ui/color/chrome_color_mixer.cc.
  mixer[kColorTabCloseButtonFocusRingActive] = ui::PickGoogleColor(
      ui::kColorFocusableBorderFocused, kColorTabBackgroundActiveFrameActive,
      color_utils::kMinimumVisibleContrastRatio);
  mixer[kColorTabCloseButtonFocusRingInactive] = ui::PickGoogleColor(
      ui::kColorFocusableBorderFocused, kColorTabBackgroundInactiveFrameActive,
      color_utils::kMinimumVisibleContrastRatio);
  mixer[kColorTabFocusRingActive] = ui::PickGoogleColorTwoBackgrounds(
      ui::kColorFocusableBorderFocused, kColorTabBackgroundActiveFrameActive,
      ui::kColorFrameActive, color_utils::kMinimumVisibleContrastRatio);
  mixer[kColorTabFocusRingInactive] = ui::PickGoogleColorTwoBackgrounds(
      ui::kColorFocusableBorderFocused, kColorTabBackgroundInactiveFrameActive,
      ui::kColorFrameActive, color_utils::kMinimumVisibleContrastRatio);
}

}  // namespace views_shell
