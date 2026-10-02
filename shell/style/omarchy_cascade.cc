// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Ported from basecamp/omarchy bin/omarchy-theme-color at c05d9019 (MIT; the
// notice is in omarchy_cascade.h). The order of the steps below is the order of
// resolve_theme_colors, step for step; each comment names the script's block.

#include "views_shell/style/omarchy_cascade.h"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <optional>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "third_party/skia/include/core/SkColor.h"

namespace views_shell {
namespace {

// The bash associative array, with its semantics: reading a missing key gives
// "", a key counts as set only when non-empty, and assigning creates the key
// even when the value is "".
class Palette {
 public:
  explicit Palette(ThemePalette colors) : colors_(std::move(colors)) {}

  std::string Get(std::string_view key) const {
    auto it = colors_.find(key);
    return it == colors_.end() ? std::string() : it->second;
  }
  bool IsSet(std::string_view key) const { return !Get(key).empty(); }
  void Set(std::string_view key, std::string value) {
    colors_.insert_or_assign(std::string(key), std::move(value));
  }
  // alias_theme_color: [[ ${C[key]} ]] || C[key]="${C[fallback]}"
  void Alias(std::string_view key, std::string_view fallback) {
    if (!IsSet(key)) {
      Set(key, Get(fallback));
    }
  }
  // [[ ${C[key]} ]] || C[key]="<value>"
  void Default(std::string_view key, std::string value) {
    if (!IsSet(key)) {
      Set(key, std::move(value));
    }
  }
  // "${C[first]:-<otherwise>}"
  std::string Or(std::string_view first, std::string otherwise) const {
    return IsSet(first) ? Get(first) : std::move(otherwise);
  }

  ThemePalette Take() && { return std::move(colors_); }

 private:
  ThemePalette colors_;
};

using KeyPair = std::pair<std::string_view, std::string_view>;

// legacy_palette_alias: canonical name -> legacy short name.
constexpr std::array<KeyPair, 8> kLegacyPaletteAlias = {{
    {"background", "bg"},
    {"dark_background", "dark_bg"},
    {"darker_background", "darker_bg"},
    {"lighter_background", "lighter_bg"},
    {"foreground", "fg"},
    {"dark_foreground", "dark_fg"},
    {"light_foreground", "light_fg"},
    {"bright_foreground", "bright_fg"},
}};

// legacy_alias: semantic name <- ANSI colorN.
constexpr std::array<KeyPair, 12> kLegacyAlias = {{
    {"red", "color1"},
    {"green", "color2"},
    {"yellow", "color3"},
    {"blue", "color4"},
    {"magenta", "color5"},
    {"cyan", "color6"},
    {"bright_red", "color9"},
    {"bright_green", "color10"},
    {"bright_yellow", "color11"},
    {"bright_blue", "color12"},
    {"bright_magenta", "color13"},
    {"bright_cyan", "color14"},
}};

// The six bright hues derived as +20 % white.
constexpr std::array<KeyPair, 6> kBrightHues = {{
    {"bright_red", "red"},
    {"bright_yellow", "yellow"},
    {"bright_green", "green"},
    {"bright_cyan", "cyan"},
    {"bright_blue", "blue"},
    {"bright_magenta", "magenta"},
}};

// ansi_alias: colorN <- semantic name.
constexpr std::array<KeyPair, 16> kAnsiAlias = {{
    {"color0", "background"},
    {"color1", "red"},
    {"color2", "green"},
    {"color3", "yellow"},
    {"color4", "blue"},
    {"color5", "magenta"},
    {"color6", "cyan"},
    {"color7", "foreground"},
    {"color8", "muted"},
    {"color9", "bright_red"},
    {"color10", "bright_green"},
    {"color11", "bright_yellow"},
    {"color12", "bright_blue"},
    {"color13", "bright_magenta"},
    {"color14", "bright_cyan"},
    {"color15", "bright_foreground"},
}};

// awk: hex_value(char) = index("0123456789abcdef", tolower(char)) - 1. gawk's
// index() of an empty needle is 1, so a missing digit (a short or empty value)
// counts as 0; a character that is not a hex digit counts as -1.
int HexDigitValue(std::string_view hex, size_t position) {
  if (position >= hex.size()) {
    return 0;
  }
  constexpr std::string_view kDigits = "0123456789abcdef";
  const size_t found = kDigits.find(base::ToLowerASCII(hex[position]));
  return found == std::string_view::npos ? -1 : static_cast<int>(found);
}

// awk: hex_pair_to_int(hex, idx), with a 0-based `position`.
int HexPair(std::string_view hex, size_t position) {
  return HexDigitValue(hex, position) * 16 + HexDigitValue(hex, position + 1);
}

// awk: int(start * (1 - amount) + end * amount + 0.5), printed with "%02x".
// int() truncates toward zero, and gawk prints a negative integer as its
// 64-bit two's complement, so the port does both.
std::string MixChannel(int start, int end, double amount) {
  const int64_t value =
      static_cast<int64_t>(start * (1 - amount) + end * amount + 0.5);
  return base::StringPrintf("%02" PRIx64, static_cast<uint64_t>(value));
}

}  // namespace

std::string MixOmarchyColor(std::string_view start,
                            std::string_view end,
                            int percent) {
  // ${1#\#}: one leading "#" is removed.
  if (start.starts_with('#')) {
    start.remove_prefix(1);
  }
  if (end.starts_with('#')) {
    end.remove_prefix(1);
  }
  // awk: amount = amount / 100, clamped to [0, 1].
  double amount = percent / 100.0;
  if (amount < 0) {
    amount = 0;
  }
  if (amount > 1) {
    amount = 1;
  }
  return base::StrCat({"#", MixChannel(HexPair(start, 0), HexPair(end, 0), amount),
                       MixChannel(HexPair(start, 2), HexPair(end, 2), amount),
                       MixChannel(HexPair(start, 4), HexPair(end, 4), amount)});
}

ThemePalette ResolveOmarchyPalette(const ThemePalette& raw,
                                   bool light_mode_marker) {
  Palette c(raw);

  // The complete legacy short-name palette first; canonical names win.
  for (const auto& [canonical, legacy] : kLegacyPaletteAlias) {
    c.Alias(canonical, legacy);
  }

  // Themes from before the semantic palette may define only ANSI names.
  if (!c.IsSet("background")) {
    c.Set("background", c.Get("color0"));
  }
  if (!c.IsSet("foreground")) {
    c.Set("foreground", c.Get("color7"));
  }
  if (c.IsSet("background")) {
    c.Set("color0", c.Get("background"));
  }
  if (c.IsSet("foreground")) {
    c.Set("color7", c.Get("foreground"));
  }

  // Legacy compatibility: ANSI color1..color14 to semantic names.
  for (const auto& [semantic, ansi] : kLegacyAlias) {
    c.Alias(semantic, ansi);
  }
  c.Alias("magenta", "purple");
  c.Alias("bright_magenta", "bright_purple");

  c.Default("light_foreground", c.Or("color7", c.Get("foreground")));
  c.Default("bright_foreground", c.Or("color15", c.Get("foreground")));
  c.Set("cursor", c.Get("bright_foreground"));
  c.Default("lighter_background", c.Or("color0", c.Get("background")));
  c.Default("dark_foreground", c.Or("color8", c.Get("foreground")));
  c.Default("muted", c.Or("color8", c.Get("dark_foreground")));
  c.Default("selection",
            c.Or("selection_background",
                 c.Or("color8", c.Or("color0", c.Get("background")))));
  c.Default("selection_background", c.Get("selection"));
  c.Default("selection_foreground", c.Get("bright_foreground"));
  c.Default("orange", c.Get("yellow"));
  if (!c.IsSet("brown")) {
    c.Set("brown", MixOmarchyColor(c.Get("orange"), "#000000", 50));
  }

  // Auto-derived shades.
  if (!c.IsSet("dark_background")) {
    c.Set("dark_background", MixOmarchyColor(c.Get("background"), "#000000", 25));
  }
  if (!c.IsSet("darker_background")) {
    c.Set("darker_background",
          MixOmarchyColor(c.Get("background"), "#000000", 50));
  }
  for (const auto& [bright, base] : kBrightHues) {
    if (!c.IsSet(bright)) {
      c.Set(bright, MixOmarchyColor(c.Get(base), "#ffffff", 20));
    }
  }
  c.Alias("purple", "magenta");
  c.Alias("bright_purple", "bright_magenta");

  // Semantic themes stay readable by consumers of the ANSI names.
  for (const auto& [ansi, semantic] : kAnsiAlias) {
    c.Alias(ansi, semantic);
  }

  // And by consumers of the short palette names.
  for (const auto& [canonical, legacy] : kLegacyPaletteAlias) {
    if (c.IsSet(canonical)) {
      c.Set(legacy, c.Get(canonical));
    }
  }

  // resolve_theme_mode: mode, legacy theme_type, light.mode, luminance, dark.
  if (!c.IsSet("mode")) {
    c.Set("mode", c.Get("theme_type"));
  }
  if (!c.IsSet("mode")) {
    if (light_mode_marker) {
      c.Set("mode", "light");
    } else if (std::optional<SkColor> background =
                   ParseHexColor(c.Get("background"))) {
      const int luminance = SkColorGetR(*background) +
                            SkColorGetG(*background) +
                            SkColorGetB(*background);
      c.Set("mode", luminance > 382 ? "light" : "dark");
    } else {
      c.Set("mode", "dark");
    }
  }
  c.Set("theme_type", c.Get("mode"));
  return std::move(c).Take();
}

}  // namespace views_shell
