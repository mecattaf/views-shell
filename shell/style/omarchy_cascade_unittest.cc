// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The byte fixtures: style/tokens/fixtures/<theme>.resolved.json is Omarchy's
// own resolver run on examples/themes/<theme>/colors.toml, and
// <theme>.mixer.json the pins style/theme-map.json makes of it
// (tools/theme-goldens.sh). The C++ cascade and mixer must reproduce both,
// compared as canonical JSON.

#include "views_shell/style/omarchy_cascade.h"

#include <optional>
#include <set>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "views_shell/style/theme_mixer.h"

namespace views_shell {
namespace {

base::FilePath DataRoot() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data");
}

base::FilePath FixtureDir() {
  return DataRoot().AppendASCII("style/tokens/fixtures");
}

// The file parsed and written back: key order and whitespace drop out.
std::string CanonicalJsonFile(const base::FilePath& path) {
  std::string text;
  if (!base::ReadFileToString(path, &text)) {
    ADD_FAILURE() << "cannot read " << path;
    return std::string();
  }
  std::optional<base::Value> value = base::JSONReader::Read(text, 0);
  if (!value || !value->is_dict()) {
    ADD_FAILURE() << path << " is not a JSON object";
    return std::string();
  }
  return base::WriteJson(*value).value_or(std::string());
}

std::string CanonicalJson(const base::DictValue& dict) {
  return base::WriteJson(dict).value_or(std::string());
}

base::DictValue PaletteAsDict(const ThemePalette& palette) {
  base::DictValue dict;
  for (const auto& [key, value] : palette) {
    dict.Set(key, value);
  }
  return dict;
}

// Requires the cascade and the mixer to reproduce `theme`'s two fixtures.
void ExpectFixtureReproduced(const std::string& theme_name) {
  SCOPED_TRACE(theme_name);
  base::expected<ThemeDirectory, std::string> theme = LoadThemeDirectory(
      DataRoot().AppendASCII("examples/themes").AppendASCII(theme_name));
  ASSERT_TRUE(theme.has_value()) << theme.error();

  const ThemePalette resolved =
      ResolveOmarchyPalette(theme->colors, theme->light_mode_marker);
  EXPECT_EQ(CanonicalJson(PaletteAsDict(resolved)),
            CanonicalJsonFile(
                FixtureDir().AppendASCII(theme_name + ".resolved.json")));

  EXPECT_EQ(
      CanonicalJson(PinsAsDict(ResolveTheme(*theme))),
      CanonicalJsonFile(FixtureDir().AppendASCII(theme_name + ".mixer.json")));
}

TEST(ThemeFixtureTest, AllBlack) {
  ExpectFixtureReproduced("all-black");
}

TEST(ThemeFixtureTest, ClaudeDark) {
  ExpectFixtureReproduced("claude-dark");
}

TEST(ThemeFixtureTest, ClaudeLight) {
  ExpectFixtureReproduced("claude-light");
}

TEST(ThemeFixtureTest, Noir) {
  ExpectFixtureReproduced("noir");
}

// Whatever fixtures the directory holds, each is reproduced, and every example
// theme has its pair.
TEST(ThemeFixtureTest, EveryFixtureInTheDirectory) {
  std::set<std::string> fixtures;
  base::FileEnumerator files(FixtureDir(), /*recursive=*/false,
                             base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.resolved.json"));
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    std::string name = path.BaseName().MaybeAsASCII();
    name.resize(name.size() - std::string_view(".resolved.json").size());
    fixtures.insert(name);
    ExpectFixtureReproduced(name);
  }
  std::set<std::string> themes;
  base::FileEnumerator dirs(DataRoot().AppendASCII("examples/themes"),
                            /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = dirs.Next(); !path.empty(); path = dirs.Next()) {
    themes.insert(path.BaseName().MaybeAsASCII());
  }
  EXPECT_EQ(fixtures, themes);
  EXPECT_EQ(fixtures.size(), 4u);
}

// Values below are tools/omarchy-theme-color's own output for these inputs.
TEST(ThemeCascadeTest, MixesLikeOmarchy) {
  EXPECT_EQ(MixOmarchyColor("#1a1a1a", "#000000", 50), "#0d0d0d");
  EXPECT_EQ(MixOmarchyColor("#f47b85", "#ffffff", 20), "#f6959d");
  EXPECT_EQ(MixOmarchyColor("#F47B85", "#FFFFFF", 20), "#f6959d");
  EXPECT_EQ(MixOmarchyColor("#f0f0f0", "#000000", 25), "#b4b4b4");
  // GNU awk reads a missing digit as 0.
  EXPECT_EQ(MixOmarchyColor("", "#ffffff", 20), "#333333");
  EXPECT_EQ(MixOmarchyColor("", "#000000", 50), "#000000");
}

TEST(ThemeCascadeTest, AnsiOnlyTheme) {
  const ThemePalette resolved = ResolveOmarchyPalette(
      {{"color0", "#101010"},
       {"color7", "#e0e0e0"},
       {"color8", "#808080"},
       {"color1", "#ff0000"},
       {"color3", "#c0a000"}},
      /*light_mode_marker=*/false);
  EXPECT_EQ(resolved.at("background"), "#101010");
  EXPECT_EQ(resolved.at("foreground"), "#e0e0e0");
  EXPECT_EQ(resolved.at("muted"), "#808080");
  EXPECT_EQ(resolved.at("dark_foreground"), "#808080");
  EXPECT_EQ(resolved.at("selection"), "#808080");
  EXPECT_EQ(resolved.at("red"), "#ff0000");
  EXPECT_EQ(resolved.at("bright_red"), "#ff3333");
  EXPECT_EQ(resolved.at("orange"), "#c0a000");
  EXPECT_EQ(resolved.at("brown"), "#605000");
  EXPECT_EQ(resolved.at("bright_yellow"), "#cdb333");
  EXPECT_EQ(resolved.at("bright_cyan"), "#333333");
  EXPECT_EQ(resolved.at("cyan"), "");
  EXPECT_EQ(resolved.at("color2"), "");
  EXPECT_EQ(resolved.at("color15"), "#e0e0e0");
  EXPECT_EQ(resolved.at("dark_background"), "#0c0c0c");
  EXPECT_EQ(resolved.at("darker_background"), "#080808");
  EXPECT_EQ(resolved.at("mode"), "dark");
  EXPECT_EQ(resolved.at("theme_type"), "dark");
  EXPECT_EQ(resolved.size(), 55u);
}

TEST(ThemeCascadeTest, ModeFromLuminanceAndMarker) {
  ThemePalette light = ResolveOmarchyPalette(
      {{"background", "#f0f0f0"}, {"foreground", "#202020"},
       {"red", "#f47b85"}},
      /*light_mode_marker=*/false);
  EXPECT_EQ(light.at("mode"), "light");
  EXPECT_EQ(light.at("bright_red"), "#f6959d");
  EXPECT_EQ(light.at("dark_background"), "#b4b4b4");
  EXPECT_EQ(light.at("selection"), "#f0f0f0");

  // Legacy short names, and the light.mode marker over a dark background.
  ThemePalette marked = ResolveOmarchyPalette(
      {{"bg", "#202020"}, {"fg", "#d0d0d0"}}, /*light_mode_marker=*/true);
  EXPECT_EQ(marked.at("background"), "#202020");
  EXPECT_EQ(marked.at("foreground"), "#d0d0d0");
  EXPECT_EQ(marked.at("dark_background"), "#181818");
  EXPECT_EQ(marked.at("mode"), "light");

  // An explicit mode wins over the marker; theme_type is the legacy key.
  EXPECT_EQ(ResolveOmarchyPalette({{"theme_type", "dark"}}, true).at("mode"),
            "dark");
}

}  // namespace
}  // namespace views_shell
