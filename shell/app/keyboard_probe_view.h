// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The credential modal's risk, probed (--demo-keyboard; rule R25). The real
// modal (NetworkManager, BlueZ and polkit secrets in one ui::DialogModel) is
// later work; what it depends on is that typed keys reach a views control on
// a layer surface: wl_keyboard -> Ozone's WaylandKeyboard -> the aura
// WindowTreeHost of a widget the compositor activated through keyboard focus
// (WaylandLayerShellWindow::OnKeyboardFocusChanged in
// patches/views-shell-ozone-layer-shell.patch) -> the focused views::Textfield.
//
// The view is a prompt label over a stock views::Textfield (plain text, never
// a password field: the probe types a known word). It lives on the "modal"
// SurfaceSpec (overlay layer, centred, 480x120, exclusive keyboard). Every
// change of the field's text is logged as "TYPED <text>", every activation
// change of its widget as "keyboard-probe: widget active <0|1>", the field's
// focus as "keyboard-probe: textfield focused <0|1>" and every key press that
// reaches the field as "keyboard-probe: key <DOM code>", so a headless run that
// types through zwp_virtual_keyboard_v1 (wtype) can tell where a key stopped.

#ifndef VIEWS_SHELL_APP_KEYBOARD_PROBE_VIEW_H_
#define VIEWS_SHELL_APP_KEYBOARD_PROBE_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace ui {
class KeyEvent;
}

namespace views {
class Textfield;
}

namespace views_shell {

class KeyboardProbeView : public views::View,
                          public views::TextfieldController,
                          public views::WidgetObserver {
  METADATA_HEADER(KeyboardProbeView, views::View)

 public:
  KeyboardProbeView();
  KeyboardProbeView(const KeyboardProbeView&) = delete;
  KeyboardProbeView& operator=(const KeyboardProbeView&) = delete;
  ~KeyboardProbeView() override;

  views::Textfield* textfield() { return textfield_; }

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  // Logs each key press that reaches the field ("keyboard-probe: key ...")
  // and lets the field handle it.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

 private:
  void LogTextfieldFocus();

  raw_ptr<views::Textfield> textfield_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_APP_KEYBOARD_PROBE_VIEW_H_
