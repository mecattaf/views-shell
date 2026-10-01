// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/shared/tab_strip_types.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_STRIP_TYPES_H_
#define VIEWS_SHELL_TABS_TAB_STRIP_TYPES_H_

namespace views_shell {

enum class TabStripOrientation {
  kHorizontal,
  kVertical,
};

// From chrome/browser/ui/tabs/tab_enums.h (Copyright 2021 The Chromium
// Authors).
enum class CloseTabSource {
  // Tab was closed by a mouse event on the tab or its close button
  kFromMouse,
  // Tab was closed by a touch event on the tab or its close button
  kFromTouch,
  // Tab is closed by some means other than direct tab interaction
  kFromNonUIEvent,
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_STRIP_TYPES_H_
