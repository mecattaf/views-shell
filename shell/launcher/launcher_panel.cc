// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/launcher_panel.cc
//
// Adapted from src/agency/shell/rescued/views_shell_launcher_panel.cc -- see the
// PROVENANCE block and delta list (D1-D6) in launcher_panel.h. The layout body
// (MakeKeyCap / MakeHint / footer / search row / separator / per-row layout /
// selection highlight math) is preserved from the spike-proven tree; the
// changes are producer wiring (live Query/Activate), token-clean colors, and
// the dropped full-screen root.

#include "agency/shell/launcher_panel.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>

#include "agency/producers/launcher/launcher_producer.h"
#include "agency/producers/launcher/launcher_result.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace agency {

namespace {

// --- Frosted-glass panel fill: the ONE documented raw-color exception --------
// (launcher-design.md §3, option (a), supervisor-recommended.) This ARGB fill's
// alpha (0xCC) is load-bearing: niri's blur-through -- proven by the spike
// (SPIKE-RESULT.md signal #2) -- requires partial transparency, and no opaque
// M3 semantic ColorId provides it. Every OTHER color in this panel is a
// token-clean ui::ColorId (see below); this single named constant is the
// deliberate, isolated exception. Verified: ui::ColorVariant has an implicit
// ColorVariant(SkColor) ctor (color_variant.h:29 @150), so it flows into the
// CreateRoundedRectBackground(ui::ColorVariant, ...) overloads unchanged.
constexpr SkColor kLauncherGlassFill = SkColorSetARGB(0xCC, 0x1c, 0x1c, 0x1e);

// Icon-swatch accent palette (delta D4): stand-ins for real freedesktop
// icon-theme assets, which are out of scope this pass. These are icon-IDENTITY
// colors (like favicons), not theme tokens -- inherently non-semantic -- so
// they are intentionally NOT ColorIds. Chosen deterministically per provider so
// each provider's rows read as one visual family.
constexpr SkColor kSwatchPalette[] = {
    SkColorSetRGB(0x34, 0xC7, 0x59),  // green
    SkColorSetRGB(0x0A, 0x84, 0xFF),  // blue
    SkColorSetRGB(0xFF, 0x9F, 0x0A),  // orange
    SkColorSetRGB(0xBF, 0x5A, 0xF2),  // purple
    SkColorSetRGB(0xFF, 0x37, 0x5F),  // pink
    SkColorSetRGB(0x64, 0xD2, 0xFF),  // teal
};

// Deterministic swatch color for a provider id (D4).
SkColor SwatchFor(const std::string& provider_id) {
  size_t h = std::hash<std::string>{}(provider_id);
  return kSwatchPalette[h % std::size(kSwatchPalette)];
}

constexpr float kPanelRadius = 14.f;
constexpr int kRowHeight = 56;
constexpr int kSearchHeight = 58;
constexpr int kFooterHeight = 44;

gfx::FontList Sized(int delta) {
  return gfx::FontList().DeriveWithSizeDelta(delta);
}

// A rounded "key cap" (the arrow keys / Enter / Esc glyphs in the footer).
std::unique_ptr<views::Label> MakeKeyCap(const std::u16string& glyph) {
  auto cap = std::make_unique<views::Label>(glyph);
  cap->SetAutoColorReadabilityEnabled(false);
  cap->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  cap->SetFontList(Sized(-1));
  cap->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysNeutralContainer, 5.f));
  cap->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));
  return cap;
}

// A footer hint: one or two key caps followed by a description label.
std::unique_ptr<views::View> MakeHint(
    const std::vector<std::u16string>& caps,
    const std::u16string& desc) {
  auto group = std::make_unique<views::View>();
  auto* box = group->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 5));
  box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  for (const auto& c : caps)
    group->AddChildView(MakeKeyCap(c));
  auto* label = group->AddChildView(std::make_unique<views::Label>(desc));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  label->SetFontList(Sized(-1));
  return group;
}

}  // namespace

// ---------------------------------------------------------------------------
// LauncherPanel
// ---------------------------------------------------------------------------

LauncherPanel::LauncherPanel(LauncherProducer* producer)
    : producer_(producer) {
  SetPreferredSize(gfx::Size(kLauncherPanelWidth, kLauncherPanelHeight));
  SetBackground(
      views::CreateRoundedRectBackground(kLauncherGlassFill, kPanelRadius));

  auto* box = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 10)));
  box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // --- Search field row -----------------------------------------------------
  auto search_row = std::make_unique<views::View>();
  search_row->SetPreferredSize(gfx::Size(0, kSearchHeight));
  auto* srow_box =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 14)));
  srow_box->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto search = std::make_unique<views::Textfield>();
  search->SetController(this);
  // Token-clean: SetTextColorId (150 replaces the rescued SetTextColor(SkColor);
  // see VERIFIED-APIS.md). Transparent background so the glass shows through.
  search->SetTextColorId(ui::kColorSysOnSurface);
  search->SetBackgroundColor(SK_ColorTRANSPARENT);
  search->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search->SetFontList(Sized(8));
  search->SetCursorEnabled(true);
  // A focusable Textfield must expose an accessible name or placeholder —
  // accessibility_paint_checks.cc DCHECKs otherwise (observed live on the
  // first --agency-launcher run). The placeholder doubles as the visual
  // affordance for the empty query state.
  search->SetPlaceholderText(u"Search apps and commands");
  search->GetViewAccessibility().SetName(u"Search apps and commands");
  search_ = search.get();
  auto* search_ptr = search_row->AddChildView(std::move(search));
  srow_box->SetFlexForView(search_ptr, 1);
  AddChildView(std::move(search_row));

  // --- Separator ------------------------------------------------------------
  auto sep = std::make_unique<views::View>();
  sep->SetPreferredSize(gfx::Size(0, 1));
  sep->SetBackground(views::CreateSolidBackground(ui::kColorSysDivider));
  AddChildView(std::move(sep));

  // --- Result rows container (D2: live rows replace the hard-coded five) -----
  auto results = std::make_unique<views::View>();
  auto* rbox = results->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  rbox->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  results_container_ = results.get();
  auto* results_ptr = AddChildView(std::move(results));
  // Absorb vertical slack so the footer stays pinned to the bottom regardless
  // of row count (the surface stays a fixed 760x399, D5).
  box->SetFlexForView(results_ptr, 1);

  // --- Footer hints ---------------------------------------------------------
  auto footer = std::make_unique<views::View>();
  footer->SetPreferredSize(gfx::Size(0, kFooterHeight));
  auto* fbox = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 14), 0));
  fbox->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  footer->AddChildView(MakeHint({u"↑", u"↓"}, u"to navigate"));
  auto* spacer1 = footer->AddChildView(std::make_unique<views::View>());
  footer->AddChildView(MakeHint({u"Enter"}, u"to select"));
  auto* spacer2 = footer->AddChildView(std::make_unique<views::View>());
  footer->AddChildView(MakeHint({u"Esc"}, u"to close"));
  fbox->SetFlexForView(spacer1, 1);
  fbox->SetFlexForView(spacer2, 1);
  AddChildView(std::move(footer));

  // Browse mode: empty query => AppProvider returns the installed app list.
  RebuildResults(std::u16string());
}

LauncherPanel::~LauncherPanel() = default;

void LauncherPanel::ContentsChanged(views::Textfield* sender,
                                    const std::u16string& new_contents) {
  RebuildResults(new_contents);
}

void LauncherPanel::RebuildResults(const std::u16string& query) {
  ClearRows();
  results_ = producer_->Query(base::UTF16ToUTF8(query));
  if (results_.size() > static_cast<size_t>(kLauncherMaxRows))
    results_.resize(kLauncherMaxRows);
  for (const auto& r : results_)
    AddResultRow(r);
  SetSelection(0);
  // Fixed surface: relayout the rows in place, no widget resize (D5).
  results_container_->InvalidateLayout();
}

void LauncherPanel::ClearRows() {
  // Drop the raw_ptrs BEFORE the row Views are freed so BackupRefPtr's
  // dangling-pointer detector does not fire at free time.
  rows_.clear();
  results_container_->RemoveAllChildViews();
}

void LauncherPanel::AddResultRow(const LauncherResult& r) {
  auto row = std::make_unique<views::View>();
  row->SetPreferredSize(gfx::Size(0, kRowHeight));
  auto* box = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 12), 12));
  box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  // Leading icon: a rounded provider-colored swatch standing in for a real
  // freedesktop icon (D4: r.icon is ignored this pass).
  auto* icon = row->AddChildView(std::make_unique<views::View>());
  icon->SetPreferredSize(gfx::Size(30, 30));
  icon->SetBackground(
      views::CreateRoundedRectBackground(SwatchFor(r.provider_id), 7.f));

  // Text column (primary title + optional secondary detail), vertically
  // centered.
  auto text_col = std::make_unique<views::View>();
  auto* col_box = text_col->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  col_box->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  auto* primary_label = text_col->AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(r.title)));
  primary_label->SetAutoColorReadabilityEnabled(false);
  primary_label->SetEnabledColor(ui::kColorSysOnSurface);
  primary_label->SetFontList(Sized(2));
  primary_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  if (!r.details.empty()) {
    auto* sec = text_col->AddChildView(
        std::make_unique<views::Label>(base::UTF8ToUTF16(r.details)));
    sec->SetAutoColorReadabilityEnabled(false);
    sec->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    sec->SetFontList(Sized(-1));
    sec->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
  auto* text_col_ptr = row->AddChildView(std::move(text_col));
  box->SetFlexForView(text_col_ptr, 1);

  // Right-aligned action chip: the provider id (e.g. "app"/"window"/"session").
  // Cheap and honest; a friendly verb map is a follow-up.
  auto* chip = row->AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(r.provider_id)));
  chip->SetAutoColorReadabilityEnabled(false);
  chip->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  chip->SetFontList(Sized(-1));
  chip->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysNeutralContainer, 6.f));
  chip->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));

  rows_.push_back(results_container_->AddChildView(std::move(row)));
}

bool LauncherPanel::HandleKeyEvent(views::Textfield* sender,
                                   const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed)
    return false;
  switch (key_event.key_code()) {
    case ui::VKEY_DOWN:
      MoveSelection(1);
      return true;
    case ui::VKEY_UP:
      MoveSelection(-1);
      return true;
    case ui::VKEY_ESCAPE:
      RequestClose();
      return true;
    case ui::VKEY_RETURN:
      // D3: Enter now runs the Launch verb and closes (was a no-op spike LOG).
      ActivateSelected();
      RequestClose();
      return true;
    default:
      return false;
  }
}

void LauncherPanel::ActivateSelected() {
  if (selected_index_ < 0 ||
      selected_index_ >= static_cast<int>(results_.size())) {
    return;
  }
  const LauncherResult& r = results_[selected_index_];
  // The Launch verb (launcher-design.md §4). A stale/unknown provider_id is a
  // silent no-op inside the producer -- the panel never fabricates ids.
  producer_->Activate(r.provider_id, r.result_id);
}

void LauncherPanel::MoveSelection(int delta) {
  if (rows_.empty())
    return;
  int n = static_cast<int>(rows_.size());
  SetSelection(((selected_index_ + delta) % n + n) % n);
}

void LauncherPanel::SetSelection(int index) {
  if (rows_.empty())
    return;
  int n = static_cast<int>(rows_.size());
  selected_index_ = std::clamp(index, 0, n - 1);
  for (int i = 0; i < n; ++i) {
    if (i == selected_index_) {
      rows_[i]->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysStateHoverOnSubtle, gfx::RoundedCornersF(8.f),
          gfx::Insets::VH(3, 6)));
    } else {
      rows_[i]->SetBackground(nullptr);
    }
  }
}

void LauncherPanel::RequestClose() {
  if (GetWidget())
    GetWidget()->Hide();
}

BEGIN_METADATA(LauncherPanel)
END_METADATA

}  // namespace agency
