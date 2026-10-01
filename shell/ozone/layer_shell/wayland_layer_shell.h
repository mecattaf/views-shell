// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// agency Ozone Patch: Layer-shell global registry handler.
// Chromium target path: ui/ozone/platform/wayland/host/wayland_layer_shell.h
//
// LIFTED VERBATIM (logic-faithful) from the proven cowl substrate:
//   cowl/patches/ozone/wayland_layer_shell.h
// See PROVENANCE.md in this directory. Only include-path/comment framing
// was adapted to the agency tree; the buildflag name is intentionally left
// as ENABLE_COWL_LAYER_SHELL (ratified D1.3 -- same flag the warm base
// uses; the //cowl/build:cowl_buildflags target is not renamed).
//
// Wraps the zwlr_layer_shell_v1 Wayland global, which allows clients to
// create layer surfaces anchored to output edges at specific z-layers.
// Uses the GlobalObjectRegistrar<T> pattern (like overlay_prioritizer.h)
// since layer-shell is an optional compositor extension.

#ifndef UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_H_
#define UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_H_

#include <cstdint>
#include <string>

#include "base/component_export.h"
#include "cowl/build/cowl_buildflags.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"

#if BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

namespace ui {

class WaylandConnection;

// Wraps the zwlr_layer_shell_v1 global, which provides the ability to
// create layer surfaces. Layer surfaces are rendered at a specific z-depth
// relative to normal windows and can be anchored to output edges.
//
// This global is optional: not all compositors support wlr-layer-shell.
// When unavailable, WaylandConnection::layer_shell() returns nullptr and
// kLayerShell windows cannot be created.
class COMPONENT_EXPORT(OZONE) WaylandLayerShell
    : public wl::GlobalObjectRegistrar<WaylandLayerShell> {
 public:
  static constexpr char kInterfaceName[] = "zwlr_layer_shell_v1";
  static constexpr uint32_t kMinVersion = 1;
  static constexpr uint32_t kMaxVersion = 5;

  static void Instantiate(WaylandConnection* connection,
                          wl_registry* registry,
                          uint32_t name,
                          const std::string& interface,
                          uint32_t version);

  WaylandLayerShell(zwlr_layer_shell_v1* layer_shell,
                    WaylandConnection* connection);
  WaylandLayerShell(const WaylandLayerShell&) = delete;
  WaylandLayerShell& operator=(const WaylandLayerShell&) = delete;
  ~WaylandLayerShell();

  // Returns the underlying protocol object for creating layer surfaces.
  zwlr_layer_shell_v1* wl_object() const { return layer_shell_.get(); }

  // The negotiated protocol version (1-5). Determines available features:
  //   v2: set_layer (dynamic layer change)
  //   v3: global destroy
  //   v4: on_demand keyboard interactivity
  //   v5: set_exclusive_edge
  uint32_t version() const { return version_; }

 private:
  wl::Object<zwlr_layer_shell_v1> layer_shell_;
  uint32_t version_ = 0;
};

}  // namespace ui

#endif  // BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

#endif  // UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_LAYER_SHELL_H_
