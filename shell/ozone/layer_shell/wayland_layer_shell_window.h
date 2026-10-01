// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// agency Ozone Patch: Layer-shell window implementation.
// Chromium target path:
//   ui/ozone/platform/wayland/host/wayland_layer_shell_window.h
//
// LIFTED VERBATIM (logic-faithful) from the proven cowl substrate:
//   cowl/patches/ozone/wayland_layer_shell_window.h
// See PROVENANCE.md in this directory.
//
// WaylandLayerShellWindow is a sibling to WaylandToplevelWindow in the
// WaylandWindow class hierarchy. It creates a zwlr_layer_surface_v1 role
// instead of xdg_toplevel, enabling shell content to be rendered as desktop
// shell components (bars, docks, OSDs, overlays, notifications, lockscreens).
//
// The critical implementation detail is the configure-before-buffer state
// machine: the GPU surface (EGL) must NOT be created until the compositor
// sends the first configure event. This is enforced by IsSurfaceConfigured()
// returning false until the first configure+ack cycle completes.

#ifndef UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_WINDOW_H_
#define UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_WINDOW_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "cowl/build/cowl_buildflags.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/platform_window/platform_window_init_properties.h"

#if BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

struct zwlr_layer_surface_v1;
struct ext_background_effect_surface_v1;

namespace ui {

class WaylandConnection;

// A WaylandWindow subclass that creates a layer-shell surface using the
// zwlr_layer_shell_v1 protocol. Layer surfaces are anchored to output
// edges and rendered at specific z-layers (background, bottom, top, overlay).
//
// This is used by agency to render shell content as desktop shell components
// (bars, docks, OSDs, overlays, notifications, lock screens).
//
// Property changes are double-buffered and take effect on the next
// wl_surface.commit, matching the Wayland protocol semantics.
class COMPONENT_EXPORT(OZONE) WaylandLayerShellWindow : public WaylandWindow {
 public:
  WaylandLayerShellWindow(PlatformWindowDelegate* delegate,
                          WaylandConnection* connection);
  WaylandLayerShellWindow(const WaylandLayerShellWindow&) = delete;
  WaylandLayerShellWindow& operator=(const WaylandLayerShellWindow&) = delete;
  ~WaylandLayerShellWindow() override;

  // Layer-shell property setters. Can be called after creation.
  // Changes are double-buffered and take effect on next wl_surface.commit.
  void SetLayer(LayerShellLayer layer);
  void SetAnchor(uint32_t anchor_bitfield);
  void SetExclusiveZone(int32_t zone);
  void SetMargin(int32_t top, int32_t right, int32_t bottom, int32_t left);
  void SetKeyboardInteractivity(LayerShellKeyboardInteractivity mode);

  // Blur support via ext-background-effect-v1 protocol.
  // Pass a vector of rects for partial blur, or std::nullopt to remove blur.
  // Rects are in surface-local coordinates (CSS pixels). DPI scaling is
  // applied by the caller (the agency surface manager) before reaching this
  // method.
  void SetBlurRegion(std::optional<std::vector<gfx::Rect>> region_px);

  // Agency (enabler #4, issue #4): NET-NEW, not lifted from cowl. See
  // PROVENANCE.md. Read-only accessor for the raw zwlr_layer_surface_v1 role
  // object so xdg_popup.cc can parent an xdg_popup onto this layer surface via
  // zwlr_layer_surface_v1_get_popup(). Pure const getter over the existing
  // private handle -- it reads state, never mutates, and does not participate
  // in the configure-before-buffer chain.
  zwlr_layer_surface_v1* layer_surface() const { return layer_surface_.get(); }

  // WaylandWindow override: down-cast used by GetXdgParentWindow() and
  // XdgPopup::Initialize() to recognise a layer-shell parent. Net-new.
  WaylandLayerShellWindow* AsWaylandLayerShellWindow() override;

  // WaylandWindow overrides:
  void HandleSurfaceConfigure(uint32_t serial) override;
  void OnSequencePoint(int64_t seq) override;
  bool IsSurfaceConfigured() override;
  void AckConfigure(uint32_t serial) override;

  bool OnInitialize(PlatformWindowInitProperties properties,
                    PlatformWindowDelegate::State* state) override;

  void Show(bool inactive) override;
  void Hide() override;
  bool IsVisible() const override;
  bool IsActive() const override;

  // Agency (issue #8): layer surfaces are excluded from the
  // WaylandToplevelWindow activation machinery (WaylandWindowManager's
  // UpdateToplevelActivation only refreshes WaylandToplevelWindow roots), so
  // the aura WindowTreeHost is never told the surface became active when the
  // compositor grants it keyboard focus. Without that activation the widget's
  // FocusController never activates the content window, and KeyEvents dispatched
  // to this host's WindowEventDispatcher are dropped -- typed text never reaches
  // the focused Textfield. Override the base keyboard-focus hook to additionally
  // deliver the activation change to our delegate.
  void OnKeyboardFocusChanged(bool focused) override;

  void SetTitle(const std::u16string& title) override;

  // These are no-ops for layer surfaces: the compositor manages
  // positioning and stacking. Layer surfaces do not have window states.
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override {}
  void Maximize() override {}
  void Minimize() override {}
  void Restore() override {}

  void Activate() override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override {}
  void SizeConstraintsChanged() override {}
  bool ShouldUpdateWindowShape() const override;
  void SetWindowGeometry(const PlatformWindowDelegate::State& state) override;
  std::string GetWindowUniqueId() const override;

  base::WeakPtr<WaylandWindow> AsWeakPtr() override;
  void DumpState(std::ostream& out) const override;

 private:
  // Creates the zwlr_layer_surface_v1 protocol object from the existing
  // wl_surface, sets all initial properties, and performs the empty commit
  // required before the compositor sends the first configure event.
  bool CreateLayerSurface();

  // Destroys the layer surface, blur surface, and resets configuration state.
  // Called from Hide() and the destructor.
  void DestroyLayerSurface();

  // WaylandWindow protected override:
  void UpdateWindowMask() override;

  // zwlr_layer_surface_v1 listener callbacks (static, C-style).
  static void OnConfigure(void* data,
                          zwlr_layer_surface_v1* surface,
                          uint32_t serial,
                          uint32_t width,
                          uint32_t height);
  static void OnClosed(void* data,
                       zwlr_layer_surface_v1* surface);

  // Layer-shell protocol object. Created in Show(), destroyed in Hide().
  wl::Object<zwlr_layer_surface_v1> layer_surface_;

  // Blur effect surface (optional, created on first SetBlurRegion call).
  wl::Object<ext_background_effect_surface_v1> blur_surface_;

  // Configure state machine flag. Set true after the first configure+ack
  // cycle completes. Gates IsSurfaceConfigured() which prevents the GPU
  // process from creating an EGL surface too early.
  bool is_configured_ = false;

  // The size assigned by the compositor in the most recent configure event.
  // For stretched dimensions (anchored to opposite edges), the compositor
  // provides the actual pixel size. Stored in surface-local coordinates.
  gfx::Size configured_size_;

  // Layer-shell properties stored from init and dynamic updates.
  LayerShellLayer layer_ = LayerShellLayer::kTop;
  uint32_t anchor_ = kLayerShellAnchorNone;
  int32_t exclusive_zone_ = 0;
  LayerShellKeyboardInteractivity keyboard_interactivity_ =
      LayerShellKeyboardInteractivity::kNone;
  gfx::Insets margin_;
  std::string layer_namespace_;

  // Target output. std::nullopt means compositor picks (NULL in protocol).
  std::optional<int64_t> target_output_id_;

  base::WeakPtrFactory<WaylandLayerShellWindow> weak_ptr_factory_{this};
};

}  // namespace ui

#endif  // BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

#endif  // UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_WINDOW_H_
