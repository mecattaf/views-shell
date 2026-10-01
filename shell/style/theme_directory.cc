// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/theme_directory.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace views_shell {
namespace {

constexpr char kBlanks[] = " \t";

bool IsKeyChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-';
}

// Omarchy's value charset (bin/omarchy-theme-color, parse_colors_file).
bool IsValueChar(char c) {
  if (base::IsAsciiAlphaNumeric(c)) {
    return true;
  }
  switch (c) {
    case '#':
    case '(':
    case ')':
    case ',':
    case '.':
    case '_':
    case '+':
    case '/':
    case '%':
    case ' ':
    case '-':
      return true;
    default:
      return false;
  }
}

bool IsQuote(char c) {
  return c == '"' || c == '\'';
}

std::string LineError(size_t line, std::string_view why) {
  return base::StringPrintf("line %zu: %.*s", line,
                            static_cast<int>(why.size()), why.data());
}

// Reads a small optional one-line file; std::nullopt when it does not exist.
std::optional<std::string> ReadNameFile(const base::FilePath& path) {
  std::string text;
  if (!base::ReadFileToString(path, &text)) {
    return std::nullopt;
  }
  std::string_view first = text;
  if (size_t newline = first.find('\n'); newline != std::string_view::npos) {
    first = first.substr(0, newline);
  }
  return std::string(base::TrimWhitespaceASCII(first, base::TRIM_ALL));
}

}  // namespace

base::expected<ThemePalette, std::string> ParseColorsToml(
    std::string_view text) {
  std::vector<std::pair<std::string, std::string>> entries;
  std::vector<std::string_view> lines = base::SplitStringPiece(
      text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  // "a\n" splits into "a" and a final empty piece, which is blank anyway.
  for (size_t index = 0; index < lines.size(); ++index) {
    const size_t line_number = index + 1;
    std::string_view line = lines[index];
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    std::string_view trimmed = base::TrimString(line, kBlanks, base::TRIM_ALL);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    if (trimmed.front() == '[') {
      return base::unexpected(
          LineError(line_number, "tables are not part of colors.toml"));
    }
    const size_t equals = trimmed.find('=');
    if (equals == std::string_view::npos) {
      return base::unexpected(LineError(line_number, "expected key = value"));
    }

    std::string_view key =
        base::TrimString(trimmed.substr(0, equals), kBlanks, base::TRIM_ALL);
    if (key.size() >= 2 && IsQuote(key.front()) && key.back() == key.front()) {
      key = key.substr(1, key.size() - 2);
    }
    if (key.empty() || !std::ranges::all_of(key, IsKeyChar)) {
      return base::unexpected(
          LineError(line_number, "a key is [A-Za-z0-9_-]+"));
    }

    std::string_view value =
        base::TrimString(trimmed.substr(equals + 1), kBlanks, base::TRIM_ALL);
    std::string_view word;
    if (!value.empty() && IsQuote(value.front())) {
      // Omarchy ends the value at the next quote of either kind.
      const size_t close = value.find_first_of("\"'", 1);
      if (close == std::string_view::npos) {
        return base::unexpected(LineError(line_number, "unterminated string"));
      }
      word = value.substr(1, close - 1);
      std::string_view rest =
          base::TrimString(value.substr(close + 1), kBlanks, base::TRIM_ALL);
      if (!rest.empty() && rest.front() != '#') {
        return base::unexpected(
            LineError(line_number, "only a # comment may follow a value"));
      }
    } else {
      word = value;
      if (word.empty() || word.find_first_of(kBlanks) != std::string_view::npos) {
        return base::unexpected(LineError(
            line_number, "a bare value is one word; quote anything else"));
      }
    }
    if (!std::ranges::all_of(word, IsValueChar)) {
      return base::unexpected(LineError(
          line_number, "a value is [A-Za-z0-9#(),._+/% -]* (Omarchy's charset)"));
    }
    for (const auto& [seen, unused] : entries) {
      if (seen == key) {
        return base::unexpected(LineError(
            line_number, base::StrCat({"duplicate key ", std::string(key)})));
      }
    }
    entries.emplace_back(std::string(key), std::string(word));
  }
  return ThemePalette(std::move(entries));
}

std::optional<SkColor> ParseHexColor(std::string_view text) {
  if (text.size() != 7 || text.front() != '#') {
    return std::nullopt;
  }
  uint32_t rgb = 0;
  for (char c : text.substr(1)) {
    if (!base::IsHexDigit(c)) {
      return std::nullopt;
    }
    rgb = rgb * 16 + base::HexDigitToInt(c);
  }
  return SkColorSetRGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

std::string ToHexColor(SkColor color) {
  return base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

ThemeDirectory::ThemeDirectory() = default;
ThemeDirectory::ThemeDirectory(const ThemeDirectory&) = default;
ThemeDirectory& ThemeDirectory::operator=(const ThemeDirectory&) = default;
ThemeDirectory::ThemeDirectory(ThemeDirectory&&) = default;
ThemeDirectory& ThemeDirectory::operator=(ThemeDirectory&&) = default;
ThemeDirectory::~ThemeDirectory() = default;

base::expected<ThemeDirectory, std::string> LoadThemeDirectory(
    const base::FilePath& dir) {
  ThemeDirectory theme;
  theme.path = dir;

  const base::FilePath colors_path = dir.AppendASCII("colors.toml");
  std::string colors_text;
  if (!base::ReadFileToString(colors_path, &colors_text)) {
    return base::unexpected(colors_path.value() + ": cannot be read");
  }
  base::expected<ThemePalette, std::string> colors =
      ParseColorsToml(colors_text);
  if (!colors.has_value()) {
    return base::unexpected(colors_path.value() + ": " + colors.error());
  }
  theme.colors = std::move(colors).value();

  theme.light_mode_marker = base::PathExists(dir.AppendASCII("light.mode"));

  const base::FilePath seed_path = dir.AppendASCII("chromium.theme");
  if (std::optional<std::string> seed = ReadNameFile(seed_path)) {
    std::vector<std::string_view> parts = base::SplitStringPiece(
        *seed, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    int channels[3] = {0, 0, 0};
    bool valid = parts.size() == 3;
    for (size_t i = 0; valid && i < 3; ++i) {
      valid = base::StringToInt(parts[i], &channels[i]) && channels[i] >= 0 &&
              channels[i] <= 255;
    }
    if (!valid) {
      return base::unexpected(seed_path.value() + ": expected r,g,b");
    }
    theme.chromium_theme =
        SkColorSetRGB(channels[0], channels[1], channels[2]);
  }

  theme.gtk_theme = ReadNameFile(dir.AppendASCII("gtk.theme"));
  theme.icons_theme = ReadNameFile(dir.AppendASCII("icons.theme"));
  return theme;
}

}  // namespace views_shell
