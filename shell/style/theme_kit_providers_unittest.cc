// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"
#include "views_shell/style/layout_provider.h"
#include "views_shell/style/typography_provider.h"

namespace views_shell {
namespace {

TEST(ThemeKitProvidersTest, DefaultsAreStockViews) {
  views::LayoutProvider stock;
  ShellLayoutProvider kit;
  // The kit registers itself as the process's provider.
  EXPECT_EQ(views::LayoutProvider::Get(), &kit);
  for (int metric : {views::DISTANCE_RELATED_CONTROL_HORIZONTAL,
                     views::DISTANCE_RELATED_CONTROL_VERTICAL,
                     views::DISTANCE_UNRELATED_CONTROL_HORIZONTAL}) {
    EXPECT_EQ(kit.GetDistanceMetric(metric), stock.GetDistanceMetric(metric));
  }
  EXPECT_EQ(kit.GetInsetsMetric(views::INSETS_DIALOG),
            stock.GetInsetsMetric(views::INSETS_DIALOG));
  EXPECT_EQ(kit.GetTypographyProvider()
                .GetFontDetails(views::style::CONTEXT_LABEL,
                                views::style::STYLE_PRIMARY)
                .size_delta,
            stock.GetTypographyProvider()
                .GetFontDetails(views::style::CONTEXT_LABEL,
                                views::style::STYLE_PRIMARY)
                .size_delta);
}

TEST(ThemeKitProvidersTest, SpacingScaleAndFontDelta) {
  views::LayoutProvider stock;
  ShellLayoutProvider kit({.spacing_scale = 2.0, .base_size_delta = 3});
  const int distance =
      stock.GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_HORIZONTAL);
  EXPECT_EQ(kit.GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_HORIZONTAL),
            2 * distance);
  const gfx::Insets insets = stock.GetInsetsMetric(views::INSETS_DIALOG);
  EXPECT_EQ(kit.GetInsetsMetric(views::INSETS_DIALOG).top(), 2 * insets.top());
  EXPECT_EQ(kit.GetInsetsMetric(views::INSETS_DIALOG).left(),
            2 * insets.left());

  const int stock_delta =
      stock.GetTypographyProvider()
          .GetFontDetails(views::style::CONTEXT_LABEL,
                          views::style::STYLE_PRIMARY)
          .size_delta;
  EXPECT_EQ(kit.GetTypographyProvider()
                .GetFontDetails(views::style::CONTEXT_LABEL,
                                views::style::STYLE_PRIMARY)
                .size_delta,
            stock_delta + 3);
}

}  // namespace
}  // namespace views_shell
