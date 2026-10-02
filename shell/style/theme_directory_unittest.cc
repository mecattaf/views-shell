// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/theme_directory.h"

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views_shell {
namespace {

base::FilePath ExampleTheme(std::string_view name) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("views_shell/data/examples/themes")
      .AppendASCII(name);
}

TEST(ThemeDirectoryTest, ParsesTheSubset) {
  constexpr char kText[] =
      "# a comment\n"
      "\n"
      "   \t\n"
      "mode = \"dark\"\n"
      "accent = \"#70b8ff\"   # inline comment\n"
      "'quoted_key'='#000000'\n"
      "\"other-key\" = \"rgb(1, 2, 3)\"\n"
      "bare = 12.5\n"
      "empty = \"\"\n"
      "crlf = \"#ABCDEF\"\r\n"
      "last = \"no newline\"";
  base::expected<ThemePalette, std::string> palette = ParseColorsToml(kText);
  ASSERT_TRUE(palette.has_value()) << palette.error();
  EXPECT_EQ(palette->size(), 8u);
  EXPECT_EQ(palette->at("mode"), "dark");
  EXPECT_EQ(palette->at("accent"), "#70b8ff");
  EXPECT_EQ(palette->at("quoted_key"), "#000000");
  EXPECT_EQ(palette->at("other-key"), "rgb(1, 2, 3)");
  EXPECT_EQ(palette->at("bare"), "12.5");
  EXPECT_EQ(palette->at("empty"), "");
  EXPECT_EQ(palette->at("crlf"), "#ABCDEF");
  EXPECT_EQ(palette->at("last"), "no newline");
}

TEST(ThemeDirectoryTest, RejectsEverythingOutsideTheSubset) {
  struct Case {
    const char* text;
    const char* error_prefix;
  } cases[] = {
      {"[bar]\n", "line 1: tables"},
      {"ok = \"#000000\"\njust words\n", "line 2: expected key = value"},
      {"bad key = \"#000000\"\n", "line 1: a key"},
      {"= \"#000000\"\n", "line 1: a key"},
      {"a = two words\n", "line 1: a bare value"},
      {"a =\n", "line 1: a bare value"},
      {"a = \"#000000\" trailing\n", "line 1: only a # comment"},
      {"a = \"#000000\n", "line 1: unterminated string"},
      {"a = \"x&y\"\n", "line 1: a value"},
      {"a = \"x\\\\y\"\n", "line 1: a value"},
      {"a = [1,2]\n", "line 1: a value"},
      {"a = \"#000000\"\nb = \"#ffffff\"\na = \"#111111\"\n",
       "line 3: duplicate key a"},
  };
  for (const Case& c : cases) {
    base::expected<ThemePalette, std::string> palette =
        ParseColorsToml(c.text);
    ASSERT_FALSE(palette.has_value()) << c.text;
    EXPECT_TRUE(palette.error().starts_with(c.error_prefix))
        << c.text << " -> " << palette.error();
  }
}

TEST(ThemeDirectoryTest, ParsesHexColours) {
  EXPECT_EQ(ParseHexColor("#1a2B3c"), SkColorSetRGB(0x1a, 0x2b, 0x3c));
  EXPECT_EQ(ParseHexColor("#000000"), SK_ColorBLACK);
  EXPECT_FALSE(ParseHexColor("1a2b3c"));
  EXPECT_FALSE(ParseHexColor("#1a2b3"));
  EXPECT_FALSE(ParseHexColor("#1a2b3g"));
  EXPECT_FALSE(ParseHexColor(""));
  EXPECT_EQ(ToHexColor(SkColorSetRGB(0x1a, 0x2b, 0x3c)), "#1a2b3c");
}

TEST(ThemeDirectoryTest, LoadsAnExampleTheme) {
  base::expected<ThemeDirectory, std::string> theme =
      LoadThemeDirectory(ExampleTheme("claude-dark"));
  ASSERT_TRUE(theme.has_value()) << theme.error();
  EXPECT_EQ(theme->colors.at("background"), "#1a1a1a");
  EXPECT_EQ(theme->colors.at("mode"), "dark");
  // Extra keys no Omarchy consumer reads are kept.
  EXPECT_EQ(theme->colors.at("nvim_catppuccin_flavour"), "mocha");
  EXPECT_FALSE(theme->light_mode_marker);
  EXPECT_FALSE(theme->chromium_theme);
  EXPECT_EQ(theme->gtk_theme, "MacTahoe-Claude-Dark-orange");
  EXPECT_EQ(theme->icons_theme, "MacTahoe-dark");
}

TEST(ThemeDirectoryTest, ReadsTheOptionalFiles) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("colors.toml"),
                              "background = \"#202020\"\n"));
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("light.mode"), ""));
  // Omarchy ships chromium.theme without a trailing newline.
  ASSERT_TRUE(
      base::WriteFile(dir.GetPath().AppendASCII("chromium.theme"), "12,11,12"));
  base::expected<ThemeDirectory, std::string> theme =
      LoadThemeDirectory(dir.GetPath());
  ASSERT_TRUE(theme.has_value()) << theme.error();
  EXPECT_TRUE(theme->light_mode_marker);
  EXPECT_EQ(theme->chromium_theme, SkColorSetRGB(12, 11, 12));
  EXPECT_FALSE(theme->gtk_theme);

  ASSERT_TRUE(
      base::WriteFile(dir.GetPath().AppendASCII("chromium.theme"), "12,11"));
  EXPECT_FALSE(LoadThemeDirectory(dir.GetPath()).has_value());
  ASSERT_TRUE(
      base::WriteFile(dir.GetPath().AppendASCII("chromium.theme"), "1,2,256"));
  EXPECT_FALSE(LoadThemeDirectory(dir.GetPath()).has_value());
}

TEST(ThemeDirectoryTest, FailsWithoutColorsToml) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::expected<ThemeDirectory, std::string> theme =
      LoadThemeDirectory(dir.GetPath());
  ASSERT_FALSE(theme.has_value());
  EXPECT_NE(theme.error().find("colors.toml: cannot be read"),
            std::string::npos);
}

}  // namespace
}  // namespace views_shell
