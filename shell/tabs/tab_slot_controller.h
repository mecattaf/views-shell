// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Ported to views-shell from chrome/browser/ui/views/tabs/tab_slot_controller.h @ 154.0.8037.92.

#ifndef VIEWS_SHELL_TABS_TAB_SLOT_CONTROLLER_H_
#define VIEWS_SHELL_TABS_TAB_SLOT_CONTROLLER_H_

#include <string>

#include "views_shell/tabs/tab_strip_types.h"
#include "views_shell/tabs/tab_style.h"

namespace ui {
class Event;
class MouseEvent;
}  // namespace ui

namespace views {
class View;
}

namespace views_shell {

class Tab;
class TabSlotView;

// Controller for tabs. The subset of Chrome's TabSlotController that a Tab
// without a TabStripModel needs: no multi-selection, drag, groups, splits,
// hover cards, alerts, throbbers or Browser.
class TabSlotController {
 public:
  // Selects the tab. `event` is the event that causes `tab` to be selected.
  virtual void SelectTab(Tab* tab, const ui::Event& event) = 0;

  // Closes the tab.
  virtual void CloseTab(Tab* tab, CloseTabSource source) = 0;

  // Returns whether `tab` may show a close button. Replaces Chrome's
  // ChromeOS OnTask lock check.
  virtual bool CanCloseTab(const Tab* tab) const = 0;

  // Returns the number of tabs in the strip.
  virtual int GetTabCount() const = 0;

  // Returns true if `tab` is the active tab. The active tab is the one whose
  // content is shown in the browser.
  virtual bool IsActiveTab(const TabSlotView* tab) const = 0;

  // Returns true if the specified Tab is selected.
  virtual bool IsTabSelected(const TabSlotView* tab) const = 0;

  // Returns the tab `offset` places from `tab`, or nullptr.
  virtual Tab* GetAdjacentTab(const Tab* tab, int offset) = 0;

  // Invoked when a mouse event occurs on `source`.
  virtual void OnMouseEventInTab(views::View* source,
                                 const ui::MouseEvent& event) = 0;

  // Shows a hover animation for `tab`.
  virtual void ShowHover(Tab* tab, TabStyle::ShowHoverStyle style) = 0;

  // Hides the hover animation for `tab`.
  virtual void HideHover(Tab* tab, TabStyle::HideHoverStyle style) = 0;

  // Returns the thickness of the stroke around all tabs in DIP.  Returns 0 if
  // there is no stroke.
  virtual int GetStrokeThickness() const = 0;

  // Returns whether the shapes of background tabs are visible against the
  // frame.
  virtual bool IsGlassFrame() const = 0;

  // Returns the accessible tab name for this tab.
  virtual std::u16string GetAccessibleTabName(const Tab* tab) const = 0;

  // Returns opacity for use on tab hover radial highlight.
  virtual float GetHoverOpacityForTab(float range_parameter) const = 0;

  // Returns opacity for use on tab hover radial highlight.
  virtual float GetHoverOpacityForRadialHighlight() const = 0;

 protected:
  virtual ~TabSlotController() = default;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_TABS_TAB_SLOT_CONTROLLER_H_
