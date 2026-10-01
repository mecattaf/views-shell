// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The registry of named surfaces (rule R1: Views only on layer-shell
// surfaces). Every top-level window views-shell creates is one row here: a
// zwlr_layer_surface_v1 with a fixed layer, anchors, size, exclusive zone,
// keyboard mode and namespace. ShellViewsDelegate::OnBeforeWidgetInit calls
// CheckSurfaceSpec() on every Widget, which CHECKs that each top-level
// TYPE_WINDOW / TYPE_WINDOW_FRAMELESS widget carries the layer-shell request of
// a registered spec, so no code path can create an xdg_toplevel. Popups, menus,
// bubbles and tooltips are not top-level windows: they become xdg_popup
// children of a layer surface and need no row.
//
// A new surface (the launcher, the notification column, the OSD) adds a row
// to kSurfaceSpecs in surface_spec.cc and nothing else.

#ifndef VIEWS_SHELL_APP_SURFACE_SPEC_H_
#define VIEWS_SHELL_APP_SURFACE_SPEC_H_

#include <cstdint>
#include <string_view>

#include "base/containers/span.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/views/widget/widget.h"

namespace views_shell {

struct SurfaceSpec {
  // The name code asks for (FindSurfaceSpec).
  std::string_view name;
  ui::LayerShellLayer layer;
  // ui::kLayerShellAnchor* bits. An axis anchored on both edges is stretched
  // by the compositor, and its size below only seeds the first layout.
  uint32_t anchor;
  int width;
  int height;
  int exclusive_zone;
  ui::LayerShellKeyboardInteractivity keyboard;
  // zwlr_layer_shell_v1.get_layer_surface's namespace; unique per row.
  std::string_view layer_namespace;
};

// Every registered surface.
base::span<const SurfaceSpec> GetSurfaceSpecs();

// The spec named `name`, or nullptr.
const SurfaceSpec* FindSurfaceSpec(std::string_view name);

// The layer-shell request `spec` describes.
ui::LayerShellProperties ToLayerShellProperties(const SurfaceSpec& spec);

// Fills `params` for a top-level widget on `spec`: frameless, the layer-shell
// request and the initial bounds.
void ApplySurfaceSpec(const SurfaceSpec& spec,
                      views::Widget::InitParams* params);

// Rule R1's second enforcement. CHECK-fails unless `params` is not a
// top-level window, or carries exactly the layer-shell request of a registered
// spec (matched by namespace, then compared field by field).
void CheckSurfaceSpec(const views::Widget::InitParams& params);

}  // namespace views_shell

#endif  // VIEWS_SHELL_APP_SURFACE_SPEC_H_
