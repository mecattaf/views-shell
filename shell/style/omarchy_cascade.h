// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A C++ port of Omarchy's colors.toml cascade, so that a derived key means the
// same thing in views-shell as in Omarchy (rule R17, style/tokens/README.md).
// Ported from basecamp/omarchy bin/omarchy-theme-color at
// c05d90196fc0dd5c21e2e797d80ffcc60d5e39fa (resolve_theme_colors,
// resolve_theme_mode and mix_color; vendored unmodified as
// tools/omarchy-theme-color), under the MIT licence below.
//
// The fixtures in style/tokens/fixtures/<theme>.resolved.json are that script's
// --all output for every example theme; omarchy_cascade_unittest.cc requires
// ResolveOmarchyPalette to reproduce each one exactly.
//
// Omarchy mixes shades with awk. The port reproduces GNU awk (the awk Omarchy's
// Arch base ships) exactly, including what it does to a value that is not
// #rrggbb: a missing digit counts as 0 (so an absent red gives bright_red
// "#333333" and an absent orange gives brown "#000000", as in Omarchy), and a
// character that is not a hex digit counts as -1. mawk would differ there.
//
// ---------------------------------------------------------------------------
// Copyright (c) David Heinemeier Hansson
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------

#ifndef VIEWS_SHELL_STYLE_OMARCHY_CASCADE_H_
#define VIEWS_SHELL_STYLE_OMARCHY_CASCADE_H_

#include <string>
#include <string_view>

#include "views_shell/style/theme_directory.h"

namespace views_shell {

// Omarchy's mix_color: each channel start*(1-amount) + end*amount, rounded
// half up, as "#rrggbb" (lowercase). `percent` is 0-100. Inputs that are not
// #rrggbb are mixed the way GNU awk mixes them (see the note above).
std::string MixOmarchyColor(std::string_view start,
                            std::string_view end,
                            int percent);

// Fills every derived key the way `omarchy-theme-color --all` does: the
// legacy short names (bg, fg, ...), color0..color15, the ANSI fallbacks,
// cursor, selection_background/foreground, orange and brown, the +20 %-white
// bright_* hues, the 25 % and 50 % darker backgrounds, purple aliases, and
// mode/theme_type (the mode key, then theme_type, then `light_mode_marker`,
// then background luminance: R+G+B > 382 is light, else dark). Keys Omarchy
// creates empty stay present and empty, as in the script's output.
ThemePalette ResolveOmarchyPalette(const ThemePalette& raw,
                                   bool light_mode_marker);

}  // namespace views_shell

#endif  // VIEWS_SHELL_STYLE_OMARCHY_CASCADE_H_
