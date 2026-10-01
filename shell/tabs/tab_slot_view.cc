// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab_slot_view.cc @ 154.0.8037.92.

#include "views_shell/tabs/tab_slot_view.h"

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"

namespace views_shell {

TabSlotView::TabSlotView() = default;

TabSlotView::~TabSlotView() = default;

gfx::Rect TabSlotView::GetAnchorBoundsInScreen() const {
  gfx::Rect bounds = View::GetAnchorBoundsInScreen();

  // Slightly inset anchor bounds to let bubbles hug the tabs more closely.
  bounds.Inset(gfx::Insets::VH(2, 0));
  return bounds;
}

BEGIN_METADATA(TabSlotView)
END_METADATA

}  // namespace views_shell
