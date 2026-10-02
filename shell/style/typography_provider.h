// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The kit's TypographyProvider (style/INVENTORY.md section 2: views::
// TypographyProvider, subclassed as Chrome's ChromeTypographyProvider is). Its
// one knob is the base size: shell.toml [font] base-size, once layer 3 is read,
// becomes a size delta applied to every views::style context and style, so the
// whole kit scales together. The default, 0, is stock Views typography; colours
// stay the stock ids, which derive from the kColorSys* roles the theme pins.

#ifndef VIEWS_SHELL_STYLE_TYPOGRAPHY_PROVIDER_H_
#define VIEWS_SHELL_STYLE_TYPOGRAPHY_PROVIDER_H_

#include "ui/views/style/typography_provider.h"

namespace views_shell {

class ShellTypographyProvider : public views::TypographyProvider {
 public:
  // `base_size_delta` is added, in pixels, to every font the kit asks for.
  explicit ShellTypographyProvider(int base_size_delta = 0);
  ShellTypographyProvider(const ShellTypographyProvider&) = delete;
  ShellTypographyProvider& operator=(const ShellTypographyProvider&) = delete;
  ~ShellTypographyProvider() override;

  int base_size_delta() const { return base_size_delta_; }

 protected:
  // views::TypographyProvider:
  ui::ResourceBundle::FontDetails GetFontDetailsImpl(int context,
                                                     int style) const override;

 private:
  const int base_size_delta_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_STYLE_TYPOGRAPHY_PROVIDER_H_
