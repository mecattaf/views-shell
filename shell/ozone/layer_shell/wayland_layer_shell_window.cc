// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// agency Ozone Patch: Layer-shell window implementation.
// Chromium target path:
//   ui/ozone/platform/wayland/host/wayland_layer_shell_window.cc
//
// LIFTED VERBATIM (logic-faithful) from the proven cowl substrate:
//   cowl/patches/ozone/wayland_layer_shell_window.cc
// See PROVENANCE.md in this directory. This is the permanent rebase-carry
// substrate underneath every L2 layer-shell surface in the agency shell
// (bar/dock/overlay/notifications/osd). Do not re-stub any of this state
// machine on a future lift -- the configure-before-buffer gate below is
// the one invariant that, if dropped, reproduces the historical "black
// screen" regression (GPU/EGL surface created before the first configure
// ack reaches the compositor).
//
// Implements the WaylandLayerShellWindow class, a WaylandWindow subclass
// that uses the zwlr_layer_shell_v1 protocol to create desktop shell
// surfaces (bars, docks, overlays, etc.).
//
// Key implementation details:
// 1. Configure-before-buffer: CreateLayerSurface() performs an empty commit,
//    then waits for OnConfigure before allowing frame submission.
// 2. Constrained sizing: When anchored to opposite edges, that dimension
//    is set to 0 and the compositor determines the actual size.
// 3. Dynamic property updates: All setters check if layer_surface_ exists
//    and send protocol requests immediately (double-buffered by Wayland).
// 4. Protocol version guards: set_layer (v2+), on_demand keyboard (v4+),
//    set_exclusive_edge (v5+) are guarded by version checks.
// 5. Blur: ext-background-effect-v1 per-surface effect created on demand,
//    destroyed with the layer surface. Guard against double-attachment.

#include "ui/ozone/platform/wayland/host/wayland_layer_shell_window.h"

#include "cowl/build/cowl_buildflags.h"

#if BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

#include <wayland-client-protocol.h>
// 'namespace' is a C++ keyword but used as a parameter name in the
// wlr-layer-shell protocol XML.  Work around the generated C header.
#define namespace namespace_
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#undef namespace

#include <algorithm>
#include <ostream>

#include "base/check.h"
#include "base/logging.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_layer_shell.h"
#include "ui/ozone/platform/wayland/host/wayland_output.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_surface.h"
#include "ui/ozone/platform/wayland/host/wayland_window_manager.h"
#include "ui/platform_window/platform_window_delegate.h"

// COWL_HAS_BACKGROUND_EFFECT is a BUILD.gn define (separate from the
// BUILDFLAG(ENABLE_COWL_LAYER_SHELL) used in Chromium patches) that
// gates optional blur support independently of core layer-shell.
#if BUILDFLAG(COWL_HAS_BACKGROUND_EFFECT)
#include <ext-background-effect-v1-client-protocol.h>
#include "ui/ozone/platform/wayland/host/wayland_blur_manager.h"
#endif

namespace ui {

namespace {

// Convert agency LayerShellLayer enum to the Wayland protocol constant.
uint32_t ToWaylandLayer(LayerShellLayer layer) {
  switch (layer) {
    case LayerShellLayer::kBackground:
      return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    case LayerShellLayer::kBottom:
      return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    case LayerShellLayer::kTop:
      return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    case LayerShellLayer::kOverlay:
      return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  }
  return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

// Convert agency anchor bitmask to the Wayland protocol anchor bitmask.
// agency uses: top=1, bottom=2, left=4, right=8
// Protocol uses: top=1, bottom=2, left=4, right=8 (same values)
uint32_t ToWaylandAnchor(uint32_t anchor) {
  uint32_t wl = 0;
  if (anchor & kLayerShellAnchorTop)
    wl |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
  if (anchor & kLayerShellAnchorBottom)
    wl |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  if (anchor & kLayerShellAnchorLeft)
    wl |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  if (anchor & kLayerShellAnchorRight)
    wl |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  return wl;
}

// Compute the size to send to the compositor via set_size.
//
// Protocol rules:
// - If anchored to both left+right, width MUST be 0 (compositor decides
//   based on output width minus margins).
// - If anchored to both top+bottom, height MUST be 0 (same logic).
// - If NOT anchored to opposite edges on an axis, that dimension MUST be
//   non-zero (the client specifies the desired size).
gfx::Size ConstrainedSize(uint32_t anchor, const gfx::Size& requested_size) {
  int width = requested_size.width();
  int height = requested_size.height();

  const bool anchored_left = anchor & kLayerShellAnchorLeft;
  const bool anchored_right = anchor & kLayerShellAnchorRight;
  const bool anchored_top = anchor & kLayerShellAnchorTop;
  const bool anchored_bottom = anchor & kLayerShellAnchorBottom;

  if (anchored_left && anchored_right) {
    width = 0;  // Compositor decides width.
  } else if (width <= 0) {
    width = 1;  // Protocol requires non-zero if not stretched.
  }

  if (anchored_top && anchored_bottom) {
    height = 0;  // Compositor decides height.
  } else if (height <= 0) {
    height = 1;  // Protocol requires non-zero if not stretched.
  }

  return gfx::Size(width, height);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

WaylandLayerShellWindow::WaylandLayerShellWindow(
    PlatformWindowDelegate* delegate,
    WaylandConnection* connection)
    : WaylandWindow(delegate, connection) {}

WaylandLayerShellWindow::~WaylandLayerShellWindow() {
  DestroyLayerSurface();
}

// ---------------------------------------------------------------------------
// OnInitialize: Store properties from PlatformWindowInitProperties
// ---------------------------------------------------------------------------

bool WaylandLayerShellWindow::OnInitialize(
    PlatformWindowInitProperties properties,
    PlatformWindowDelegate::State* state) {
  // Layer surfaces don't have window states like maximized/minimized.
  state->window_state = PlatformWindowState::kNormal;

  // Store layer-shell specific properties for later use in CreateLayerSurface.
  layer_ = properties.layer_shell_layer;
  anchor_ = properties.layer_shell_anchor;
  exclusive_zone_ = properties.layer_shell_exclusive_zone;
  keyboard_interactivity_ = properties.layer_shell_keyboard_interactivity;
  margin_ = gfx::Insets::TLBR(
      properties.layer_shell_margin_top,
      properties.layer_shell_margin_left,
      properties.layer_shell_margin_bottom,
      properties.layer_shell_margin_right);
  layer_namespace_ = properties.layer_shell_namespace;
  target_output_id_ = properties.layer_shell_output_id;

  return true;
}

// ---------------------------------------------------------------------------
// CreateLayerSurface: Protocol object setup + empty commit
// ---------------------------------------------------------------------------

bool WaylandLayerShellWindow::CreateLayerSurface() {
  auto* layer_shell = connection()->layer_shell();
  if (!layer_shell) {
    LOG(ERROR) << "Compositor does not support zwlr_layer_shell_v1. "
               << "Cannot create layer shell window.";
    return false;
  }

  // Resolve the target wl_output, if specified.
  // NULL means the compositor picks (typically the most recently focused
  // output).
  wl_output* wl_output_obj = nullptr;
  if (target_output_id_.has_value()) {
    auto* output_manager = connection()->wayland_output_manager();
    if (output_manager) {
      auto* output = output_manager->GetOutput(target_output_id_.value());
      if (output) {
        wl_output_obj = output->get_output();
      } else {
        LOG(WARNING) << "Requested output " << target_output_id_.value()
                     << " not found, falling back to compositor pick";
      }
    }
  }

  // Create the layer surface from the existing wl_surface.
  // The root_surface is created by WaylandWindow::Initialize() before
  // OnInitialize() is called, so it is guaranteed to exist.
  layer_surface_.reset(zwlr_layer_shell_v1_get_layer_surface(
      layer_shell->wl_object(),
      root_surface()->surface(),
      wl_output_obj,
      ToWaylandLayer(layer_),
      layer_namespace_.c_str()));
  if (!layer_surface_) {
    LOG(ERROR) << "Failed to create zwlr_layer_surface_v1";
    return false;
  }

  // Set up listener for configure and close events.
  static constexpr zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
      .configure = &OnConfigure,
      .closed = &OnClosed,
  };
  zwlr_layer_surface_v1_add_listener(
      layer_surface_.get(), &kLayerSurfaceListener, this);

  // --- Set all properties before the initial empty commit ---

  // Size: Apply constraint rules (0 for stretched dimensions).
  gfx::Size constrained =
      ConstrainedSize(anchor_, GetBoundsInDIP().size());
  zwlr_layer_surface_v1_set_size(
      layer_surface_.get(), constrained.width(), constrained.height());

  // Anchor: Bitmask of edges this surface attaches to.
  zwlr_layer_surface_v1_set_anchor(
      layer_surface_.get(), ToWaylandAnchor(anchor_));

  // Exclusive zone: Pixels reserved along the anchored edge.
  // 0 = no reservation, -1 = don't move for other surfaces' zones.
  zwlr_layer_surface_v1_set_exclusive_zone(
      layer_surface_.get(), exclusive_zone_);

  // Margins: Offset from anchored edges (top, right, bottom, left).
  zwlr_layer_surface_v1_set_margin(
      layer_surface_.get(),
      margin_.top(), margin_.right(), margin_.bottom(), margin_.left());

  // Keyboard interactivity: none, exclusive, or on_demand.
  // The on_demand value (2) was added in protocol version 4. Sending it to
  // a pre-v4 compositor raises an invalid_keyboard_interactivity error.
  // Fall back to kNone if on_demand is requested but unsupported.
  {
    auto effective_mode = keyboard_interactivity_;
    if (effective_mode == LayerShellKeyboardInteractivity::kOnDemand &&
        zwlr_layer_surface_v1_get_version(layer_surface_.get()) < 4) {
      LOG(WARNING) << "on_demand keyboard interactivity requires layer-shell "
                   << "v4+, falling back to none";
      effective_mode = LayerShellKeyboardInteractivity::kNone;
    }
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_.get(), static_cast<uint32_t>(effective_mode));
  }

  // CRITICAL: Perform the initial empty commit (no buffer attached).
  // The compositor will respond with a configure event containing the
  // actual size this surface should use. Only after acking that configure
  // may we attach a buffer (enforced by IsSurfaceConfigured()).
  UpdateWindowMask();
  root_surface()->Commit(/*flush=*/true);

  connection()->Flush();

  VLOG(1) << "Created layer surface: namespace=" << layer_namespace_
          << " layer=" << static_cast<int>(layer_)
          << " anchor=0x" << std::hex << anchor_ << std::dec
          << " exclusive_zone=" << exclusive_zone_;

  return true;
}

// ---------------------------------------------------------------------------
// DestroyLayerSurface
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::DestroyLayerSurface() {
  // Destroy blur surface BEFORE the layer surface to avoid protocol errors.
  // The blur effect surface references the wl_surface, which is still valid
  // at this point. Destroying in this order ensures clean teardown.
  blur_surface_.reset();

  if (layer_surface_) {
    layer_surface_.reset();
    is_configured_ = false;
    configured_size_ = gfx::Size();
  }
}

// ---------------------------------------------------------------------------
// Show / Hide / Visibility
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::Show(bool inactive) {
  if (layer_surface_) {
    return;  // Already shown.
  }

  if (!CreateLayerSurface()) {
    Close();
    return;
  }

  UpdateWindowScale(false);
  WaylandWindow::Show(inactive);
}

void WaylandLayerShellWindow::Hide() {
  if (!layer_surface_) {
    return;
  }

  DestroyLayerSurface();
  WaylandWindow::Hide();

  // Clear any pending configure serials since the surface is gone.
  ClearInFlightRequestsSerial();

  connection()->Flush();
}

bool WaylandLayerShellWindow::IsVisible() const {
  return !!layer_surface_;
}

bool WaylandLayerShellWindow::IsActive() const {
  // Layer surfaces are "active" when they have keyboard focus.
  // This is determined by the compositor based on the keyboard_interactivity
  // setting and user interaction.
  return HasKeyboardFocus();
}

void WaylandLayerShellWindow::OnKeyboardFocusChanged(bool focused) {
  // Preserve the base behavior (notifies the FocusClient of the raw keyboard
  // focus change).
  WaylandWindow::OnKeyboardFocusChanged(focused);

  // Agency (issue #8): drive aura activation from keyboard focus for layer
  // surfaces. WaylandToplevelWindow reports activation to its delegate from
  // UpdateActivationState(), which is only reached via the window manager's
  // UpdateToplevelActivation() -- and that helper resolves the focused window's
  // root through AsWaylandToplevelWindow(), which returns nullptr for a layer
  // surface. As a result no activation ever reaches this window's
  // DesktopWindowTreeHostPlatform, its widget is never activated, no aura
  // window is focused in this host's dispatcher, and KeyEvents targeted at the
  // surface are dropped before they can reach the focused Textfield (observed
  // live: wl_keyboard.enter + key events arrive, yet the search field stays
  // empty). Deliver the activation change here so the widget's FocusController
  // activates the content window and restores focus to the search field on
  // keyboard enter. IsActive() (== HasKeyboardFocus()) already reports the
  // matching state to any subsequent query.
  delegate()->OnActivationChanged(focused);
}

// ---------------------------------------------------------------------------
// No-op overrides for operations that don't apply to layer surfaces
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::SetTitle(const std::u16string& title) {
  // Layer surfaces don't have titles. No-op.
}

void WaylandLayerShellWindow::Activate() {
  // Layer surface activation is managed by the compositor based on the
  // keyboard_interactivity setting. The client cannot request activation.
}

// ---------------------------------------------------------------------------
// Window geometry and identity
// ---------------------------------------------------------------------------

bool WaylandLayerShellWindow::ShouldUpdateWindowShape() const {
  // Layer surfaces are typically rectangular and don't use CSD shapes.
  return false;
}

std::string WaylandLayerShellWindow::GetWindowUniqueId() const {
  return layer_namespace_;
}

void WaylandLayerShellWindow::SetWindowGeometry(
    const PlatformWindowDelegate::State& state) {
  // Layer surfaces don't use xdg window geometry. The size is controlled
  // via set_size and the configure response. When the caller changes
  // bounds, we translate that into a set_size request.
  if (layer_surface_) {
    gfx::Size constrained =
        ConstrainedSize(anchor_, state.bounds_dip.size());
    zwlr_layer_surface_v1_set_size(
        layer_surface_.get(), constrained.width(), constrained.height());
  }
}

// ---------------------------------------------------------------------------
// Configure state machine
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::HandleSurfaceConfigure(uint32_t serial) {
  // Process the pending configure state through the standard pipeline.
  // This queues the state change which will be applied when the frame
  // callback fires (or immediately if no frame is in flight).
  ProcessPendingConfigureState(serial);
}

void WaylandLayerShellWindow::OnSequencePoint(int64_t seq) {
  if (!layer_surface_) {
    return;
  }
  ProcessSequencePoint(seq);
  MaybeApplyLatestStateRequest(/*force=*/false);
}

bool WaylandLayerShellWindow::IsSurfaceConfigured() {
  // This is the critical gate that prevents the GPU process from creating
  // an EGL surface before the compositor has sent the first configure.
  // Returning false here causes WaylandWindow to defer buffer attachment.
  return is_configured_;
}

void WaylandLayerShellWindow::AckConfigure(uint32_t serial) {
  if (!layer_surface_) {
    return;
  }

  // Send the ack to the compositor, which allows us to attach buffers.
  zwlr_layer_surface_v1_ack_configure(layer_surface_.get(), serial);

  if (!is_configured_) {
    // First configure: transition the state machine to "configured".
    // After this point, IsSurfaceConfigured() returns true and the GPU
    // process can create the EGL surface and start submitting frames.
    is_configured_ = true;
    connection()->window_manager()->NotifyWindowConfigured(this);
    VLOG(1) << "Layer surface configured: " << layer_namespace_
            << " size=" << configured_size_.ToString();
  }
}

// ---------------------------------------------------------------------------
// Misc overrides
// ---------------------------------------------------------------------------

base::WeakPtr<WaylandWindow> WaylandLayerShellWindow::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

// Agency (enabler #4, issue #4): NET-NEW, not lifted from cowl. See
// PROVENANCE.md. Lets XdgPopup::Initialize() recognise a layer-shell parent and
// reach layer_surface() for zwlr_layer_surface_v1.get_popup. Read-only; does
// not touch the configure-before-buffer state machine.
WaylandLayerShellWindow* WaylandLayerShellWindow::AsWaylandLayerShellWindow() {
  return this;
}

void WaylandLayerShellWindow::UpdateWindowMask() {
  // Layer surfaces are typically transparent/shaped by their content.
  // No window mask is needed.
}

void WaylandLayerShellWindow::DumpState(std::ostream& out) const {
  WaylandWindow::DumpState(out);
  out << ", layer_shell"
      << ", layer=" << static_cast<int>(layer_)
      << ", anchor=0x" << std::hex << anchor_ << std::dec
      << ", exclusive_zone=" << exclusive_zone_
      << ", keyboard=" << static_cast<int>(keyboard_interactivity_)
      << ", namespace=" << layer_namespace_
      << ", configured=" << is_configured_;
  if (is_configured_) {
    out << ", configured_size=" << configured_size_.ToString();
  }
}

// ---------------------------------------------------------------------------
// Dynamic property setters
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::SetLayer(LayerShellLayer layer) {
  if (layer_ == layer) {
    return;
  }
  layer_ = layer;

  if (layer_surface_) {
    // set_layer was added in protocol version 2.
    uint32_t version =
        zwlr_layer_surface_v1_get_version(layer_surface_.get());
    if (version >= ZWLR_LAYER_SURFACE_V1_SET_LAYER_SINCE_VERSION) {
      zwlr_layer_surface_v1_set_layer(
          layer_surface_.get(), ToWaylandLayer(layer));
    } else {
      // Pre-v2: must remap (destroy + recreate) the surface.
      VLOG(1) << "Layer change requires remap (protocol v" << version << ")";
      Hide();
      Show(/*inactive=*/false);
    }
  }
}

void WaylandLayerShellWindow::SetAnchor(uint32_t anchor_bitfield) {
  if (anchor_ == anchor_bitfield) {
    return;
  }
  anchor_ = anchor_bitfield;

  if (layer_surface_) {
    zwlr_layer_surface_v1_set_anchor(
        layer_surface_.get(), ToWaylandAnchor(anchor_));
    // Anchor affects which dimensions are constrained, so also update size.
    gfx::Size constrained =
        ConstrainedSize(anchor_, GetBoundsInDIP().size());
    zwlr_layer_surface_v1_set_size(
        layer_surface_.get(), constrained.width(), constrained.height());
  }
}

void WaylandLayerShellWindow::SetExclusiveZone(int32_t zone) {
  if (exclusive_zone_ == zone) {
    return;
  }
  exclusive_zone_ = zone;

  if (layer_surface_) {
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_.get(), zone);
  }
}

void WaylandLayerShellWindow::SetMargin(int32_t top, int32_t right,
                                        int32_t bottom, int32_t left) {
  gfx::Insets new_margin = gfx::Insets::TLBR(top, left, bottom, right);
  if (margin_ == new_margin) {
    return;
  }
  margin_ = new_margin;

  if (layer_surface_) {
    zwlr_layer_surface_v1_set_margin(
        layer_surface_.get(), top, right, bottom, left);
  }
}

void WaylandLayerShellWindow::SetKeyboardInteractivity(
    LayerShellKeyboardInteractivity mode) {
  if (keyboard_interactivity_ == mode) {
    return;
  }
  keyboard_interactivity_ = mode;

  if (layer_surface_) {
    auto effective_mode = mode;
    if (effective_mode == LayerShellKeyboardInteractivity::kOnDemand &&
        zwlr_layer_surface_v1_get_version(layer_surface_.get()) < 4) {
      LOG(WARNING) << "on_demand keyboard interactivity requires layer-shell "
                   << "v4+, falling back to none";
      effective_mode = LayerShellKeyboardInteractivity::kNone;
    }
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_.get(), static_cast<uint32_t>(effective_mode));
  }
}

// ---------------------------------------------------------------------------
// Blur support (ext-background-effect-v1)
// ---------------------------------------------------------------------------

void WaylandLayerShellWindow::SetBlurRegion(
    std::optional<std::vector<gfx::Rect>> region_px) {
#if BUILDFLAG(COWL_HAS_BACKGROUND_EFFECT)
  auto* effect_manager = connection()->background_effect_manager();
  if (!effect_manager || !effect_manager->supports_blur()) {
    return;
  }

  if (!region_px.has_value() || region_px->empty()) {
    // Remove blur: either no region provided or empty vector.
    if (blur_surface_) {
      ext_background_effect_surface_v1_set_blur_region(
          blur_surface_.get(), nullptr);
    }
    return;
  }

  // Create the per-surface effect object on first use.
  // Guard against double-attachment: only create once per surface.
  if (!blur_surface_) {
    blur_surface_ = effect_manager->CreateEffectSurface(
        root_surface()->surface());
    if (!blur_surface_) {
      LOG(WARNING) << "Failed to create background effect surface";
      return;
    }
  }

  // Create a wl_region and add all rects.
  wl::Object<wl_region> region(
      wl_compositor_create_region(connection()->compositor()));
  for (const auto& rect : region_px.value()) {
    wl_region_add(region.get(), rect.x(), rect.y(),
                  rect.width(), rect.height());
  }
  ext_background_effect_surface_v1_set_blur_region(
      blur_surface_.get(), region.get());
  // wl_region can be destroyed after set_blur_region; the compositor
  // takes a copy of the region data (copy semantics).
#else
  (void)region_px;
#endif
}

// ---------------------------------------------------------------------------
// Static Wayland listener callbacks
// ---------------------------------------------------------------------------

// static
void WaylandLayerShellWindow::OnConfigure(
    void* data,
    zwlr_layer_surface_v1* surface,
    uint32_t serial,
    uint32_t width,
    uint32_t height) {
  auto* self = static_cast<WaylandLayerShellWindow*>(data);
  DCHECK(self);

  // Store the configured size from the compositor.
  self->configured_size_ = gfx::Size(width, height);

  // Update pending configure state with the compositor-provided size.
  // For layer surfaces there is no window state (maximized/etc), only size.
  // The configure event provides width/height in surface-local coordinates
  // (DIP), so we must convert to pixels using the delegate for size_px.
  if (width > 0 && height > 0) {
    gfx::Rect bounds_dip(self->GetBoundsInDIP().origin(),
                         gfx::Size(width, height));
    self->pending_configure_state_.bounds_dip = bounds_dip;
    // CRITICAL: Convert DIP to pixels using the proper scale factor.
    // The layer-shell configure event delivers surface-local (DIP) sizes.
    // On a 2x scale display, a 1920-wide bar in DIP = 3840px. Using DIP
    // as pixels would cause GPU buffer size mismatch and visual corruption.
    self->pending_configure_state_.size_px =
        self->delegate()->ConvertRectToPixels(bounds_dip).size();
  }

  // Process the configure through the standard pipeline.
  // Use weak pointer to guard against destruction during processing.
  auto weak = self->AsWeakPtr();
  weak->HandleSurfaceConfigure(serial);

  if (!weak) {
    return;  // Window was destroyed during configure processing.
  }

  // Signal that the surface can now accept frames.
  // On first configure this triggers the GPU surface creation.
  weak->OnSurfaceConfigureEvent();
}

// static
void WaylandLayerShellWindow::OnClosed(
    void* data,
    zwlr_layer_surface_v1* surface) {
  auto* self = static_cast<WaylandLayerShellWindow*>(data);
  DCHECK(self);

  VLOG(1) << "Layer surface closed by compositor: " << self->layer_namespace_;

  // The compositor wants this surface gone. This can happen when:
  // - The output the surface is on is disconnected
  // - The compositor is shutting down
  // - The compositor decides the surface should be removed
  self->OnCloseRequest();
}

}  // namespace ui

#endif  // BUILDFLAG(ENABLE_COWL_LAYER_SHELL)
