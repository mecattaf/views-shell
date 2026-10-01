// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/layout_constants.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_LAYOUT_CONSTANTS_H_
#define VIEWS_SHELL_TABS_LAYOUT_CONSTANTS_H_

namespace views_shell {

// The subset of Chrome's LayoutConstant that the ported tab reads. Values are
// unchanged from upstream; see CHROME-PORT-LEDGER.md (C003).
enum class LayoutConstant {
  // Padding after the tab title.
  kTabAfterTitlePadding,

  // Width and height of a tab's close button.
  kTabCloseButtonSize,

  // The height of a tab, including outer strokes. In non-browser windows this
  // is the height of the tab strip.
  kTabHeight,

  // The total tab strip height, including all interior padding.
  kTabStripHeight,

  // The padding value shared between the area above the tab, the bottom of
  // the detached tab, and on all sides of the controls padding.
  kTabStripPadding,

  // Padding before the tab title.
  kTabPreTitlePadding,

  // The vertical padding of the tab's contents.
  kTabVerticalPadding,

  // The horizontal padding of the tab's contents.
  kTabHorizontalPadding,

  // The overlap between the tab strip and the toolbar.
  kTabstripToolbarOverlap,

  // Vertical tab strip metrics.
  kVerticalTabCornerRadius,
  kVerticalTabHeight,
  kVerticalTabMinWidth,
  kVerticalTabStripHorizontalPadding,
  kVerticalTabStripUncollapsedVerticalPadding,
};

int GetLayoutConstant(LayoutConstant constant);

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_LAYOUT_CONSTANTS_H_
