// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/bar/workspace_strip.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/expected.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"

namespace views_shell::bar {
namespace {

// The corner radius of a highlighted button, from the kit's shape metrics.
float HighlightRadius() {
  return static_cast<float>(views::LayoutProvider::Get()->GetCornerRadiusMetric(
      views::ShapeContextTokens::kButtonRadius, gfx::Size()));
}

}  // namespace

WorkspaceStrip::WorkspaceStrip(WmModel* model) : model_(model) {
  CHECK(model_);
  const views::LayoutProvider* provider = views::LayoutProvider::Get();
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0),
      provider->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL) /
          2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  observation_.Observe(model_.get());
  // A model that already holds a snapshot is shown at once; this is the
  // model's state, not a guess, so rule R6 still holds.
  if (model_->has_snapshot()) {
    Rebuild();
  }
}

WorkspaceStrip::~WorkspaceStrip() = default;

std::vector<WorkspaceStrip::ButtonState> WorkspaceStrip::GetButtonStates()
    const {
  std::vector<ButtonState> states;
  states.reserve(entries_.size());
  for (const Entry& entry : entries_) {
    states.push_back({.workspace = entry.workspace,
                      .label = std::u16string(entry.button->GetText()),
                      .focused = entry.focused,
                      .urgent = entry.urgent});
  }
  return states;
}

views::LabelButton* WorkspaceStrip::GetButtonForWorkspace(
    const std::string& id) {
  for (const Entry& entry : entries_) {
    if (entry.workspace == id) {
      return entry.button;
    }
  }
  return nullptr;
}

void WorkspaceStrip::OnSnapshotApplied() {
  Rebuild();
}

void WorkspaceStrip::OnDisconnected() {
  // The model keeps its last snapshot until the reconnect's full snapshot
  // replaces it; so does the strip.
  LOG(WARNING) << "bar: compositor connection lost; workspaces frozen until "
                  "it is back";
}

void WorkspaceStrip::Rebuild() {
  ++rebuild_count_;
  const WmSnapshot& snapshot = model_->snapshot();

  std::vector<Entry> next;
  next.reserve(snapshot.workspaces.size());
  for (const WmWorkspace& workspace : snapshot.workspaces) {
    const std::u16string label =
        base::UTF8ToUTF16(workspace.name.empty() ? workspace.id
                                                 : workspace.name);
    Entry entry{.workspace = workspace.id,
                .focused = workspace.focused,
                .urgent = workspace.urgent};
    auto old = std::ranges::find(entries_, workspace.id, &Entry::workspace);
    if (old != entries_.end()) {
      entry.button = old->button;
      entries_.erase(old);
      if (entry.button->GetText() != label) {
        entry.button->SetText(label);
      }
    } else {
      auto button = std::make_unique<views::LabelButton>(
          base::BindRepeating(&WorkspaceStrip::OnButtonPressed,
                              base::Unretained(this), workspace.id),
          label);
      button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
      button->SetBorder(nullptr);
      button->SetMinSize(gfx::Size(28, 0));
      entry.button = AddChildView(std::move(button));
    }
    entry.button->SetAccessibleName(label);
    next.push_back(std::move(entry));
  }

  // Whatever is left in entries_ is a workspace the snapshot no longer has.
  for (Entry& gone : entries_) {
    views::LabelButton* button = gone.button;
    gone.button = nullptr;
    std::unique_ptr<views::LabelButton> removed = RemoveChildViewT(button);
  }
  entries_ = std::move(next);

  for (size_t i = 0; i < entries_.size(); ++i) {
    ReorderChildView(entries_[i].button, i);
    Style(entries_[i]);
  }
  PreferredSizeChanged();
}

// static
void WorkspaceStrip::Style(const Entry& entry) {
  views::LabelButton* button = entry.button;
  if (entry.urgent) {
    button->SetEnabledTextColors(kUrgentTextId);
    button->SetBackground(
        views::CreateRoundedRectBackground(kUrgentBackgroundId,
                                           HighlightRadius()));
  } else if (entry.focused) {
    button->SetEnabledTextColors(kFocusedTextId);
    button->SetBackground(
        views::CreateRoundedRectBackground(kFocusedBackgroundId,
                                           HighlightRadius()));
  } else {
    button->SetEnabledTextColors(kTextId);
    button->SetBackground(nullptr);
  }
}

void WorkspaceStrip::OnButtonPressed(const std::string& workspace) {
  // Rule R6: ask, and wait for the echo. The strip redraws from
  // OnSnapshotApplied, never from here.
  model_->FocusWorkspace(
      workspace,
      base::BindOnce(
          [](const std::string& workspace,
             base::expected<void, WmCommandError> result) {
            LOG_IF(WARNING, !result.has_value())
                << "bar: focusing workspace " << workspace
                << " failed: " << WmCommandErrorName(result.error());
          },
          workspace));
}

BEGIN_METADATA(WorkspaceStrip)
END_METADATA

}  // namespace views_shell::bar
