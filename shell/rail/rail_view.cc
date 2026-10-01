// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/rail_view.cc

#include "agency/shell/rail_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"

namespace agency {

namespace {

// Row indentation + glyph prefixes. These are text-only so emphasis renders
// under any theme without depending on an accent colour id.
constexpr int kOuterPadDip = 8;
constexpr int kWorkspaceIndentDip = 8;
constexpr int kWindowIndentDip = 24;

// Add one left-inset Label row carrying `text`, coloured with the on-surface
// token. Returned non-owning for callers that want no further handle (none do).
void AddRow(views::View* parent, const std::u16string& text, int left_inset) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(ui::kColorSysOnSurface);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(2, left_inset, 2, 0)));
  parent->AddChildView(std::move(label));
}

}  // namespace

RailView::RailView() {
  // Rail background: same M3 "header" surface token the bar uses (proven
  // present @150, bar_view.cc). Token-clean, fully themed.
  SetBackground(views::CreateSolidBackground(ui::kColorSysHeader));

  // Vertical stack, top-aligned, rows fill the rail width. BoxLayout (not
  // FlexLayout) keeps the child list a plain top-to-bottom column that
  // SetModel() can clear + rebuild wholesale.
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kOuterPadDip), /*between_child_spacing=*/2));
}

RailView::~RailView() = default;

void RailView::SetModel(const RailModel& model) {
  RemoveAllChildViews();

  for (const RailWorkspaceItem& ws : model.workspaces) {
    // Workspace header. Active workspace gets a filled caret; urgent gets a
    // bang; others a hollow marker -- all glyph-only so no accent token is
    // needed for the emphasis to be visible in a screenshot.
    std::u16string marker = ws.is_urgent ? u"! "
                            : ws.is_active ? u"▸ "   // ▸ active
                                           : u"▹ ";  // ▹ inactive
    AddRow(this, marker + base::UTF8ToUTF16(ws.label),
           kWorkspaceIndentDip);

    for (const RailWindowItem& w : ws.windows) {
      std::u16string wmarker = w.is_focused ? u"● "   // ● focused
                                            : u"· ";  // · unfocused
      AddRow(this, wmarker + base::UTF8ToUTF16(w.label),
             kWindowIndentDip);
    }
  }

  // A rebuilt child list needs a layout+paint pass to appear.
  InvalidateLayout();
  SchedulePaint();
}

gfx::Size RailView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // The compositor fills the rail's height (anchored Top|Bottom|Left); the
  // client-chosen width is kRailWidthDip. Height defers to the available bound
  // when the layout offers one, else 0 (compositor-filled).
  const int height = available_size.height().is_bounded()
                         ? available_size.height().value()
                         : 0;
  return gfx::Size(kRailWidthDip, height);
}

}  // namespace agency
