// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab/tab_title.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_TITLE_H_
#define VIEWS_SHELL_TABS_TAB_TITLE_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"

namespace views_shell {

// The class that is the view for the tab title. This is shared
// across both horizontal and vertical tab strip.
class TabTitle : public views::Label {
  METADATA_HEADER(TabTitle, views::Label)
 public:
  TabTitle();
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_TITLE_H_
