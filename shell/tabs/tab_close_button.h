// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab/tab_close_button.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_CLOSE_BUTTON_H_
#define VIEWS_SHELL_TABS_TAB_CLOSE_BUTTON_H_

#include "base/functional/callback_forward.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/masked_targeter_delegate.h"
#include "views_shell/tabs/tab_style.h"

namespace views_shell {

// This is a Button subclass that shows the tab closed icon.
//
// In addition to setup for the icon, it forwards middle clicks to the parent
// View by explicitly not handling them in OnMousePressed.
class TabCloseButton : public views::LabelButton,
                       public views::MaskedTargeterDelegate {
  METADATA_HEADER(TabCloseButton, views::LabelButton)

 public:
  using MouseEventCallback =
      base::RepeatingCallback<void(views::View*, const ui::MouseEvent&)>;

  // The mouse_event callback will be called for every mouse event to allow
  // middle clicks to be handled by the parent.
  //
  // See note on SetTabColor.
  TabCloseButton(PressedCallback pressed_callback,
                 MouseEventCallback mouse_event_callback);
  TabCloseButton(const TabCloseButton&) = delete;
  TabCloseButton& operator=(const TabCloseButton&) = delete;
  ~TabCloseButton() override;

  TabStyle::TabColors GetColors() const;
  // This function must be called before the tab is painted so it knows what
  // colors to use. It must also be called when the background color of the tab
  // changes (this class does not track tab activation state), and when the
  // theme changes.
  void SetColors(TabStyle::TabColors colors);

  // views::LabelButton:
  View* GetTooltipHandlerForPoint(const gfx::Point& point) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  void AddLayerToRegion(ui::Layer* new_layer,
                        views::LayerRegion region) override;
  void RemoveLayerFromRegions(ui::Layer* old_layer) override;

 protected:
  // Set/reset the image models for the icon with new colors.
  void UpdateIcon();

  // views::LabelButton:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  // views::MaskedTargeterDelegate:
  views::View* TargetForRect(views::View* root, const gfx::Rect& rect) override;
  bool GetHitTestMask(SkPath* mask) const override;

  MouseEventCallback mouse_event_callback_;

  TabStyle::TabColors colors_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_CLOSE_BUTTON_H_
