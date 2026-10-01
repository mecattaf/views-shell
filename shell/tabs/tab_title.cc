// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab/tab_title.cc @ 154.0.8037.92.

#include "views_shell/tabs/tab_title.h"

#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/text_constants.h"

namespace views_shell {

TabTitle::TabTitle() {
  SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  SetElideBehavior(gfx::FADE_TAIL);
  SetHandlesTooltips(false);
  SetAutoColorReadabilityEnabled(false);
  SetBackgroundColor(SK_ColorTRANSPARENT);
  // Title paints on top of an opaque region (the tab background) of a
  // non-opaque layer (the tabstrip's layer), which cannot currently be detected
  // by the subpixel-rendering opacity check.
  SetSkipSubpixelRenderingOpacityCheck(true);
  SetCanProcessEventsWithinSubtree(false);
  SetCollapseWhenHidden(true);
}

BEGIN_METADATA(TabTitle)
END_METADATA

}  // namespace views_shell
