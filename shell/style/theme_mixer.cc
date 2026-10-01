// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/theme_mixer.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/native_theme/native_theme.h"
#include "views_shell/style/omarchy_cascade.h"

namespace views_shell {
namespace {

double RelativeLuminance(SkColor color) {
  auto linear = [](int channel) {
    const double v = channel / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(SkColorGetR(color)) +
         0.7152 * linear(SkColorGetG(color)) +
         0.0722 * linear(SkColorGetB(color));
}

}  // namespace

ResolvedTheme::ResolvedTheme() = default;
ResolvedTheme::ResolvedTheme(const ResolvedTheme&) = default;
ResolvedTheme& ResolvedTheme::operator=(const ResolvedTheme&) = default;
ResolvedTheme::ResolvedTheme(ResolvedTheme&&) = default;
ResolvedTheme& ResolvedTheme::operator=(ResolvedTheme&&) = default;
ResolvedTheme::~ResolvedTheme() = default;

SkColor NeutraliseSeed(SkColor color) {
  const int r = SkColorGetR(color);
  const int g = SkColorGetG(color);
  const int b = SkColorGetB(color);
  const int max = std::max({r, g, b});
  const int min = std::min({r, g, b});
  if (max + min <= 16 || (max >= 250 && max - min <= 4)) {
    const int average = (r + g + b) / 3;
    return SkColorSetRGB(average, average, average);
  }
  return SkColorSetRGB(r, g, b);
}

double ContrastRatio(SkColor a, SkColor b) {
  const double la = RelativeLuminance(a);
  const double lb = RelativeLuminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

ResolvedTheme ResolveTheme(const ThemeDirectory& theme) {
  ResolvedTheme resolved;
  resolved.palette =
      ResolveOmarchyPalette(theme.colors, theme.light_mode_marker);
  ThemePalette& palette = resolved.palette;
  auto color = [&palette](std::string_view key) -> std::optional<SkColor> {
    auto it = palette.find(key);
    return it == palette.end() ? std::nullopt : ParseHexColor(it->second);
  };

  // on_accent (theme-map.json): the ink that reads best on the accent.
  std::optional<SkColor> accent = color("accent");
  std::optional<SkColor> background = color("background");
  std::optional<SkColor> bright_foreground = color("bright_foreground");
  if (accent && background && bright_foreground) {
    palette.insert_or_assign(
        std::string("on_accent"),
        ContrastRatio(*accent, *background) >=
                ContrastRatio(*accent, *bright_foreground)
            ? palette.find("background")->second
            : palette.find("bright_foreground")->second);
  }

  auto mode = palette.find("mode");
  resolved.color_mode = mode != palette.end() && mode->second == "light"
                            ? ui::ColorProviderKey::ColorMode::kLight
                            : ui::ColorProviderKey::ColorMode::kDark;

  // Layer 1: chromium.theme when the directory ships one, else the background
  // (black when even that is not #rrggbb), neutralised.
  resolved.seed = NeutraliseSeed(
      theme.chromium_theme.value_or(background.value_or(SK_ColorBLACK)));

  // Layer 2.
  for (const ThemePinBinding& binding : GetThemePinBindings()) {
    if (std::optional<SkColor> value = color(binding.omarchy_key)) {
      resolved.pins.emplace_back(binding.id, *value);
    }
  }
  return resolved;
}

base::expected<ResolvedTheme, std::string> LoadAndResolveTheme(
    const base::FilePath& dir) {
  base::expected<ThemeDirectory, std::string> theme = LoadThemeDirectory(dir);
  if (!theme.has_value()) {
    return base::unexpected(theme.error());
  }
  return ResolveTheme(*theme);
}

base::DictValue PinsAsDict(const ResolvedTheme& theme) {
  base::DictValue dict;
  for (const ThemePinBinding& binding : GetThemePinBindings()) {
    for (const auto& [id, value] : theme.pins) {
      if (id == binding.id) {
        dict.Set(binding.id_name, ToHexColor(value));
      }
    }
  }
  return dict;
}

void AddThemePinMixer(const ResolvedTheme& theme, ui::ColorProvider* provider) {
  ui::ColorMixer& mixer = provider->AddMixer();
  for (const auto& [id, value] : theme.pins) {
    mixer[id] = {value};
  }
}

ThemeController::ThemeController() = default;
ThemeController::~ThemeController() = default;

void ThemeController::Apply(ResolvedTheme theme,
                            ui::NativeTheme* native_theme) {
  theme_ = std::move(theme);
  native_theme->set_preferred_color_scheme(
      theme_->color_mode == ui::ColorProviderKey::ColorMode::kDark
          ? ui::NativeTheme::PreferredColorScheme::kDark
          : ui::NativeTheme::PreferredColorScheme::kLight);
  native_theme->set_user_color(theme_->seed);

  ui::ColorProviderManager& manager = ui::ColorProviderManager::Get();
  if (!initializer_appended_) {
    // Appending resets the provider cache itself.
    manager.AppendColorProviderInitializer(base::BindRepeating(
        &ThemeController::AddMixer, weak_ptr_factory_.GetWeakPtr()));
    initializer_appended_ = true;
  } else {
    manager.ResetColorProviderCache();
  }
  native_theme->NotifyOnNativeThemeUpdated();
}

void ThemeController::AddMixer(ui::ColorProvider* provider,
                               const ui::ColorProviderKey& key) {
  if (theme_) {
    AddThemePinMixer(*theme_, provider);
  }
}

}  // namespace views_shell
