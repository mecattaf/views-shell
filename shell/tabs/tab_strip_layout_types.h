// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab_strip_layout_types.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_STRIP_LAYOUT_TYPES_H_
#define VIEWS_SHELL_TABS_TAB_STRIP_LAYOUT_TYPES_H_

namespace views_shell {

// Sizing info for individual tabs.
struct TabSizeInfo {
  // The width of pinned tabs.
  int pinned_tab_width;

  // The min width of active/inactive tabs.
  int min_active_width;
  int min_inactive_width;

  // The width of a standard tab, which is the largest size active or inactive
  // tabs ever have.
  int standard_width;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_STRIP_LAYOUT_TYPES_H_
