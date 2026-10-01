// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Reads an Omarchy theme directory (rule R17; style/tokens/README.md): the
// theme Tom passes with --theme. views-shell reads, and never writes:
//
//   colors.toml     the palette (required), parsed by ParseColorsToml below;
//   light.mode      optional marker file: its presence means a light theme
//                   when colors.toml names no mode;
//   chromium.theme  optional "r,g,b": the seed Omarchy hands Chrome, used as
//                   views-shell's layer-1 seed when present;
//   gtk.theme, icons.theme
//                   optional one-line names, kept as strings for the one
//                   gsettings writer (rule R26); views-shell never applies them.
//
// Chromium has no TOML parser, and colors.toml is not general TOML: it is a
// flat list of `key = "value"` lines. ParseColorsToml accepts exactly this
// subset, and rejects (with the line number) everything else:
//
//   * Lines are separated by "\n"; a trailing "\r" is removed. A last line
//     without a newline is read too.
//   * A blank line (spaces and tabs only) is skipped.
//   * A line whose first non-blank character is "#" is a comment.
//   * Every other line is `key = value`, split at the first "=", with spaces
//     and tabs trimmed around key and value.
//   * key matches [A-Za-z0-9_-]+, optionally wrapped in one pair of matching
//     quotes ("key" or 'key').
//   * value is either quoted, "..." or '...', running to the next quote of
//     either kind (Omarchy's own rule), after which only blanks or a "#"
//     comment may follow; or bare, which must be one word with no blanks.
//     The text between the quotes, or the bare word, must match Omarchy's
//     value charset [A-Za-z0-9#(),._+/% -]*. A quoted value may be empty.
//   * A key may appear once.
//   Rejected: TOML tables ("[section]"), arrays, inline tables, escapes,
//   multi-line strings, a missing "=", a bare value with blanks, text after a
//   closing quote, duplicate keys and any character outside the charsets.
//
// On every file Omarchy's parser accepts and this subset admits, the result is
// exactly what `omarchy-theme-color --raw` prints: Omarchy at c05d9019 skips a
// key with a bad character (with a warning) and silently drops a last line
// without a newline, where views-shell refuses the file or reads the line. All
// keys are kept, including keys no consumer knows (plugins may read them).

#ifndef VIEWS_SHELL_STYLE_THEME_DIRECTORY_H_
#define VIEWS_SHELL_STYLE_THEME_DIRECTORY_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "third_party/skia/include/core/SkColor.h"

namespace views_shell {

// Omarchy's palette: key -> value, both as written. An empty value counts as
// unset, as it does in Omarchy's bash ([[ ${THEME_COLORS[key]} ]]).
using ThemePalette = base::flat_map<std::string, std::string>;

// Parses colors.toml text (the subset above). On failure the error reads
// "line <n>: <why>".
base::expected<ThemePalette, std::string> ParseColorsToml(
    std::string_view text);

// Parses "#rrggbb" (either case). Anything else is std::nullopt.
std::optional<SkColor> ParseHexColor(std::string_view text);

// "#rrggbb", lowercase, alpha ignored.
std::string ToHexColor(SkColor color);

// One theme directory, as read from disk. Nothing is resolved yet (see
// omarchy_cascade.h for the derived keys and theme_mixer.h for the colours).
struct ThemeDirectory {
  ThemeDirectory();
  ThemeDirectory(const ThemeDirectory&);
  ThemeDirectory& operator=(const ThemeDirectory&);
  ThemeDirectory(ThemeDirectory&&);
  ThemeDirectory& operator=(ThemeDirectory&&);
  ~ThemeDirectory();

  base::FilePath path;
  // colors.toml exactly as written.
  ThemePalette colors;
  // light.mode exists beside colors.toml.
  bool light_mode_marker = false;
  // chromium.theme, "r,g,b" with each channel 0-255.
  std::optional<SkColor> chromium_theme;
  // gtk.theme and icons.theme, first line, blanks trimmed.
  std::optional<std::string> gtk_theme;
  std::optional<std::string> icons_theme;
};

// Reads `dir`. Fails, with a message naming the file, when colors.toml is
// missing or outside the subset, or when chromium.theme is present but not
// "r,g,b".
base::expected<ThemeDirectory, std::string> LoadThemeDirectory(
    const base::FilePath& dir);

}  // namespace views_shell

#endif  // VIEWS_SHELL_STYLE_THEME_DIRECTORY_H_
