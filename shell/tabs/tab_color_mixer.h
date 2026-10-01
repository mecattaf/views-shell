// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/color/material_tab_strip_color_mixer.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_COLOR_MIXER_H_
#define VIEWS_SHELL_TABS_TAB_COLOR_MIXER_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace views_shell {

// Adds the recipes for the ids in tab_color_id.h, in terms of stock ui/color
// kColorSys* ids. Register it with
// ui::ColorProviderManager::AppendColorProviderInitializer before the first
// widget is shown.
void AddTabColorMixer(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key);

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_COLOR_MIXER_H_
