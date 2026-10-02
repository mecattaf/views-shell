// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/app/keyboard_probe_view.h"

#include <memory>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"

namespace views_shell {

KeyboardProbeView::KeyboardProbeView() {
  SetBackground(views::CreateSolidBackground(ui::kColorSysSurface));
  const views::LayoutProvider* provider = views::LayoutProvider::Get();
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      provider->GetInsetsMetric(views::INSETS_DIALOG),
      provider->GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_VERTICAL)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* prompt = AddChildView(std::make_unique<views::Label>(
      u"views-shell keyboard probe: type here"));
  prompt->SetEnabledColor(ui::kColorSysOnSurface);
  prompt->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  textfield_ = AddChildView(std::make_unique<views::Textfield>());
  textfield_->set_controller(this);
  textfield_->SetAccessibleName(u"keyboard probe");
  textfield_->SetPlaceholderText(u"typed text appears here");
}

KeyboardProbeView::~KeyboardProbeView() {
  textfield_->set_controller(nullptr);
}

void KeyboardProbeView::AddedToWidget() {
  widget_observation_.Observe(GetWidget());
  // The field is the view to focus whenever the widget becomes active.
  textfield_->RequestFocus();
  LogTextfieldFocus();
}

void KeyboardProbeView::RemovedFromWidget() {
  widget_observation_.Reset();
}

void KeyboardProbeView::ContentsChanged(views::Textfield* sender,
                                        const std::u16string& new_contents) {
  LOG(INFO) << "TYPED " << base::UTF16ToUTF8(new_contents);
}

bool KeyboardProbeView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& key_event) {
  if (key_event.type() == ui::EventType::kKeyPressed) {
    LOG(INFO) << "keyboard-probe: key "
              << ui::KeycodeConverter::DomCodeToCodeString(key_event.code());
  }
  return false;
}

void KeyboardProbeView::OnWidgetActivationChanged(views::Widget* widget,
                                                  bool active) {
  LOG(INFO) << "keyboard-probe: widget active " << active;
  if (active) {
    textfield_->RequestFocus();
  }
  LogTextfieldFocus();
}

void KeyboardProbeView::LogTextfieldFocus() {
  LOG(INFO) << "keyboard-probe: textfield focused " << textfield_->HasFocus();
}

BEGIN_METADATA(KeyboardProbeView)
END_METADATA

}  // namespace views_shell
