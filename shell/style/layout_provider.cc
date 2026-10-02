// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/style/layout_provider.h"

#include <cmath>

namespace views_shell {

ShellLayoutProvider::ShellLayoutProvider(const ShellLayoutParams& params)
    : spacing_scale_(params.spacing_scale),
      typography_provider_(params.base_size_delta) {}

ShellLayoutProvider::~ShellLayoutProvider() = default;

int ShellLayoutProvider::Scale(int value) const {
  return static_cast<int>(std::lround(value * spacing_scale_));
}

gfx::Insets ShellLayoutProvider::GetInsetsMetric(int metric) const {
  const gfx::Insets insets = views::LayoutProvider::GetInsetsMetric(metric);
  return gfx::Insets::TLBR(Scale(insets.top()), Scale(insets.left()),
                           Scale(insets.bottom()), Scale(insets.right()));
}

int ShellLayoutProvider::GetDistanceMetric(int metric) const {
  return Scale(views::LayoutProvider::GetDistanceMetric(metric));
}

const views::TypographyProvider& ShellLayoutProvider::GetTypographyProvider()
    const {
  return typography_provider_;
}

}  // namespace views_shell
