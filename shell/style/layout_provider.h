// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The kit's LayoutProvider (style/INVENTORY.md section 2: views::
// LayoutProvider, subclassed as Chrome's ChromeLayoutProvider is). It owns the
// kit's ShellTypographyProvider and carries the spacing scale: shell.toml
// [spacing] scale, once layer 3 is read, multiplies every distance and inset
// metric the stock controls ask for. The default scale, 1.0, is stock Views
// spacing. The ShellViewsDelegate owns the one instance (views::LayoutProvider
// registers itself as the process's provider on construction).

#ifndef VIEWS_SHELL_STYLE_LAYOUT_PROVIDER_H_
#define VIEWS_SHELL_STYLE_LAYOUT_PROVIDER_H_

#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/layout_provider.h"
#include "views_shell/style/typography_provider.h"

namespace views_shell {

struct ShellLayoutParams {
  // Multiplies every distance and inset metric; rounded to whole pixels.
  double spacing_scale = 1.0;
  // Added to every font size (ShellTypographyProvider).
  int base_size_delta = 0;
};

class ShellLayoutProvider : public views::LayoutProvider {
 public:
  explicit ShellLayoutProvider(const ShellLayoutParams& params = {});
  ShellLayoutProvider(const ShellLayoutProvider&) = delete;
  ShellLayoutProvider& operator=(const ShellLayoutProvider&) = delete;
  ~ShellLayoutProvider() override;

  double spacing_scale() const { return spacing_scale_; }

  // views::LayoutProvider:
  gfx::Insets GetInsetsMetric(int metric) const override;
  int GetDistanceMetric(int metric) const override;
  const views::TypographyProvider& GetTypographyProvider() const override;

 private:
  int Scale(int value) const;

  const double spacing_scale_;
  ShellTypographyProvider typography_provider_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_STYLE_LAYOUT_PROVIDER_H_
