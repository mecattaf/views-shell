// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/theme_mixer.h"

#include "base/base_paths.h"
#include "base/path_service.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

namespace views_shell {
namespace {

ResolvedTheme LoadExample(std::string_view name) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  base::expected<ResolvedTheme, std::string> theme = LoadAndResolveTheme(
      root.AppendASCII("views_shell/data/examples/themes").AppendASCII(name));
  CHECK(theme.has_value()) << theme.error();
  return std::move(theme).value();
}

SkColor Hex(std::string_view text) {
  return ParseHexColor(text).value();
}

// A provider built the way ColorProviderManager builds one in the program:
// the stock mixers for the theme's key, then the pin mixer last.
std::unique_ptr<ui::ColorProvider> BuildProvider(const ResolvedTheme& theme) {
  ui::ColorProviderKey key;
  key.color_mode = theme.color_mode;
  key.user_color = theme.seed;
  key.user_color_source = ui::ColorProviderKey::UserColorSource::kAccent;
  auto provider = std::make_unique<ui::ColorProvider>();
  ui::AddColorMixers(provider.get(), key);
  AddThemePinMixer(theme, provider.get());
  return provider;
}

TEST(ThemeMixerTest, NeutralisesTheSeed) {
  EXPECT_EQ(NeutraliseSeed(SK_ColorBLACK), SK_ColorBLACK);
  // max + min <= 16: the channel average.
  EXPECT_EQ(NeutraliseSeed(SkColorSetRGB(5, 6, 8)), SkColorSetRGB(6, 6, 6));
  // max >= 250 with chroma <= 4: the channel average.
  EXPECT_EQ(NeutraliseSeed(SkColorSetRGB(250, 250, 252)),
            SkColorSetRGB(250, 250, 250));
  // Everything else is left alone.
  EXPECT_EQ(NeutraliseSeed(SkColorSetRGB(0x1a, 0x1a, 0x1a)),
            SkColorSetRGB(0x1a, 0x1a, 0x1a));
  EXPECT_EQ(NeutraliseSeed(SkColorSetRGB(0xf9, 0xf9, 0xf7)),
            SkColorSetRGB(0xf9, 0xf9, 0xf7));
  EXPECT_EQ(NeutraliseSeed(SkColorSetRGB(250, 240, 252)),
            SkColorSetRGB(250, 240, 252));
}

TEST(ThemeMixerTest, ContrastIsWcag) {
  EXPECT_DOUBLE_EQ(ContrastRatio(SK_ColorBLACK, SK_ColorWHITE), 21.0);
  EXPECT_DOUBLE_EQ(ContrastRatio(SK_ColorWHITE, SK_ColorWHITE), 1.0);
}

TEST(ThemeMixerTest, SeedAndModeOfTheExamples) {
  const ResolvedTheme claude_dark = LoadExample("claude-dark");
  EXPECT_EQ(claude_dark.color_mode, ui::ColorProviderKey::ColorMode::kDark);
  EXPECT_EQ(claude_dark.seed, Hex("#1a1a1a"));

  const ResolvedTheme claude_light = LoadExample("claude-light");
  EXPECT_EQ(claude_light.color_mode, ui::ColorProviderKey::ColorMode::kLight);
  EXPECT_EQ(claude_light.seed, Hex("#f9f9f7"));

  const ResolvedTheme all_black = LoadExample("all-black");
  EXPECT_EQ(all_black.color_mode, ui::ColorProviderKey::ColorMode::kDark);
  EXPECT_EQ(all_black.seed, SK_ColorBLACK);
  // White accent: black ink reads best on it.
  EXPECT_EQ(all_black.palette.at("on_accent"), "#000000");
  EXPECT_EQ(all_black.pins.size(), GetThemePinBindings().size());
}

TEST(ThemeMixerTest, ProviderForAllBlack) {
  const ResolvedTheme theme = LoadExample("all-black");
  std::unique_ptr<ui::ColorProvider> provider = BuildProvider(theme);
  EXPECT_EQ(provider->GetColor(ui::kColorSysBase), SK_ColorBLACK);
  EXPECT_EQ(provider->GetColor(ui::kColorSysSurface), SK_ColorBLACK);
  EXPECT_EQ(provider->GetColor(ui::kColorSysOnSurface), SK_ColorWHITE);
  EXPECT_EQ(provider->GetColor(ui::kColorSysPrimary), SK_ColorWHITE);
  EXPECT_EQ(provider->GetColor(ui::kColorSysOnPrimary), SK_ColorBLACK);
  EXPECT_EQ(provider->GetColor(ui::kColorSysOnSurfaceSecondary),
            Hex("#cccccc"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysDivider), Hex("#808080"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysStateTextHighlight),
            Hex("#3d3d3d"));
  // Ids derived from a pinned role follow the pin.
  EXPECT_EQ(provider->GetColor(ui::kColorLabelForeground), SK_ColorWHITE);
}

TEST(ThemeMixerTest, ProviderForClaudeDark) {
  const ResolvedTheme theme = LoadExample("claude-dark");
  std::unique_ptr<ui::ColorProvider> provider = BuildProvider(theme);
  EXPECT_EQ(provider->GetColor(ui::kColorSysBase), Hex("#1a1a1a"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysHeaderInactive), Hex("#151515"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysSurface3), Hex("#20201f"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysOnSurface), Hex("#eaecf0"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysPrimary), Hex("#70b8ff"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysStateFocusRing), Hex("#70b8ff"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysError), Hex("#f47b85"));
  EXPECT_EQ(provider->GetColor(ui::kColorSysOnPrimary),
            Hex(theme.palette.at("on_accent")));

  // Without the pin mixer the same key gives the stock tonal rendition, which
  // is not the theme's background: the pins are what make it exact.
  ui::ColorProviderKey key;
  key.color_mode = theme.color_mode;
  key.user_color = theme.seed;
  ui::ColorProvider stock;
  ui::AddColorMixers(&stock, key);
  EXPECT_NE(stock.GetColor(ui::kColorSysBase), Hex("#1a1a1a"));
}

class CountingObserver : public ui::NativeThemeObserver {
 public:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override {
    ++updates;
  }
  int updates = 0;
};

// A second Apply() re-themes live: new providers carry the new pins, and the
// native theme's observers hear about it.
TEST(ThemeMixerTest, LiveReapply) {
  base::test::TaskEnvironment task_environment;
  ui::ColorProviderManager::ResetForTesting();
  ui::NativeTheme* native_theme = ui::NativeTheme::GetInstanceForNativeUi();
  CountingObserver observer;
  native_theme->AddObserver(&observer);

  {
    ThemeController controller;
    controller.Apply(LoadExample("all-black"), native_theme);
    EXPECT_EQ(observer.updates, 1);
    ui::ColorProvider* provider =
        ui::ColorProviderManager::Get().GetColorProviderFor(
            native_theme->GetColorProviderKey(nullptr));
    EXPECT_EQ(provider->GetColor(ui::kColorSysBase), SK_ColorBLACK);
    EXPECT_EQ(native_theme->user_color(), SK_ColorBLACK);

    controller.Apply(LoadExample("claude-light"), native_theme);
    EXPECT_EQ(observer.updates, 2);
    EXPECT_EQ(native_theme->preferred_color_scheme(),
              ui::NativeTheme::PreferredColorScheme::kLight);
    provider = ui::ColorProviderManager::Get().GetColorProviderFor(
        native_theme->GetColorProviderKey(nullptr));
    EXPECT_EQ(provider->GetColor(ui::kColorSysBase), Hex("#f9f9f7"));
    EXPECT_EQ(provider->GetColor(ui::kColorSysOnSurface), Hex("#131313"));
  }

  native_theme->RemoveObserver(&observer);
  native_theme->set_user_color(std::nullopt);
  native_theme->set_preferred_color_scheme(
      ui::NativeTheme::PreferredColorScheme::kNoPreference);
  ui::ColorProviderManager::ResetForTesting();
}

}  // namespace
}  // namespace views_shell
