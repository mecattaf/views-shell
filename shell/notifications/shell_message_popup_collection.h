// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The notification popups of views-shell: message_center's popup collection
// with every popup a zwlr_layer_surface_v1 (rule R1). Modelled on
// ui/message_center/views/desktop_message_popup_collection.{h,cc}, which is
// read, not copied: popups stack top-down from the top right corner of the
// primary display's work area, newest last.
//
// Each popup Widget gets Widget::InitParams::layer_shell (the field
// patches/views-shell-ozone-layer-shell.patch adds) from a PopupSurfaceSpec:
// the overlay layer, anchored top and right, margins that place it in its
// stacking slot, no keyboard, namespace views-shell-notification. A layer
// surface is positioned by the compositor from its anchor and margins, so the
// margins are fixed when the popup is created (docs/notifications.md, Not
// done: restacking).

#ifndef VIEWS_SHELL_NOTIFICATIONS_SHELL_MESSAGE_POPUP_COLLECTION_H_
#define VIEWS_SHELL_NOTIFICATIONS_SHELL_MESSAGE_POPUP_COLLECTION_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/display/display_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/message_center/views/message_popup_collection.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace display {
class Screen;
}

namespace views_shell::notifications {

inline constexpr char kPopupSurfaceNamespace[] = "views-shell-notification";

// The layer surface one popup asks for: the SurfaceSpec shape of
// docs/architecture.md (layer, anchor, margins, keyboard, namespace) for the
// one surface kind this directory makes.
struct PopupSurfaceSpec {
  ui::LayerShellLayer layer = ui::LayerShellLayer::kOverlay;
  uint32_t anchor = ui::kLayerShellAnchorTop | ui::kLayerShellAnchorRight;
  int32_t margin_top = 0;
  int32_t margin_right = 0;
  int32_t margin_bottom = 0;
  int32_t margin_left = 0;
  // Popups never take the keyboard; their buttons are pointer-driven.
  ui::LayerShellKeyboardInteractivity keyboard =
      ui::LayerShellKeyboardInteractivity::kNone;
  // No exclusive zone: a popup never moves tiled windows.
  int32_t exclusive_zone = 0;
  std::string layer_namespace = kPopupSurfaceNamespace;
  gfx::Size size;

  bool operator==(const PopupSurfaceSpec&) const;
};

// The spec of a popup whose bounds (screen coordinates, as the collection
// computes them) are |popup_bounds| inside |work_area|: the margins are the
// distances from the work area's top and right edges.
PopupSurfaceSpec PopupSurfaceSpecForBounds(const gfx::Rect& popup_bounds,
                                           const gfx::Rect& work_area);

ui::LayerShellProperties ToLayerShellProperties(const PopupSurfaceSpec& spec);

class ShellMessagePopupCollection
    : public message_center::MessagePopupCollection,
      public display::DisplayObserver {
 public:
  ShellMessagePopupCollection();
  ShellMessagePopupCollection(const ShellMessagePopupCollection&) = delete;
  ShellMessagePopupCollection& operator=(const ShellMessagePopupCollection&) =
      delete;
  ~ShellMessagePopupCollection() override;

  // Reads the primary display's work area from display::Screen and follows
  // its changes. Call once a display::Screen exists (UI thread).
  void StartObserving();

  // Every spec handed to a popup Widget, in creation order. For the tests and
  // for diagnostics.
  const std::vector<PopupSurfaceSpec>& surface_specs() const {
    return surface_specs_;
  }

  // Tests only: aura test hosts need a context window for a popup Widget.
  // In the program, the ViewsDelegate makes each popup a desktop widget.
  void set_widget_context_for_testing(gfx::NativeWindow context) {
    widget_context_for_testing_ = context;
  }

  // message_center::MessagePopupCollection:
  bool RecomputeAlignment(const display::Display& display) override;
  void ConfigureWidgetInitParamsForContainer(
      views::Widget* widget,
      views::Widget::InitParams* init_params) override;

 protected:
  // message_center::MessagePopupCollection:
  int GetPopupOriginX(const gfx::Rect& popup_bounds) const override;
  int GetBaseline() const override;
  gfx::Rect GetWorkArea() const override;
  bool IsTopDown() const override;
  bool IsFromLeft() const override;
  bool IsPrimaryDisplayForNotification() const override;
  bool BlockForMixedFullscreen(
      const message_center::Notification& notification) const override;
  bool CanUseTransformForBoundsAnimation() const override;

 private:
  // The bounds the collection will give the popup being created: the next
  // slot below the existing popups.
  gfx::Rect NextPopupBounds(int popup_height);
  void UpdatePrimaryDisplay();

  // display::DisplayObserver:
  void OnDisplayAdded(const display::Display& new_display) override;
  void OnDisplaysRemoved(const display::Displays& removed_displays) override;
  void OnDisplayMetricsChanged(const display::Display& display,
                               uint32_t changed_metrics) override;

  raw_ptr<display::Screen> screen_ = nullptr;
  std::optional<display::ScopedDisplayObserver> display_observer_;
  int64_t primary_display_id_ = display::kInvalidDisplayId;
  gfx::Rect work_area_;
  std::vector<PopupSurfaceSpec> surface_specs_;
  gfx::NativeWindow widget_context_for_testing_ = gfx::NativeWindow();
};

}  // namespace views_shell::notifications

#endif  // VIEWS_SHELL_NOTIFICATIONS_SHELL_MESSAGE_POPUP_COLLECTION_H_
