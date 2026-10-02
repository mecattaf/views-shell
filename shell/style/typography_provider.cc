// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/typography_provider.h"

namespace views_shell {

ShellTypographyProvider::ShellTypographyProvider(int base_size_delta)
    : base_size_delta_(base_size_delta) {}

ShellTypographyProvider::~ShellTypographyProvider() = default;

ui::ResourceBundle::FontDetails ShellTypographyProvider::GetFontDetailsImpl(
    int context,
    int style) const {
  ui::ResourceBundle::FontDetails details =
      views::TypographyProvider::GetFontDetailsImpl(context, style);
  details.size_delta += base_size_delta_;
  return details;
}

}  // namespace views_shell
