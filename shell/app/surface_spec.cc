// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/app/surface_spec.h"

#include <array>
#include <string>

#include "base/check.h"
#include "ui/gfx/geometry/rect.h"

namespace views_shell {
namespace {

constexpr std::array<SurfaceSpec, 2> kSurfaceSpecs = {{
    // The bar (SPEC.md C4.2; open question Q2's default): top layer, the top
    // edge stretched across the output, 32 px high with an exclusive zone of
    // the same height, never taking the keyboard.
    {
        .name = "bar",
        .layer = ui::LayerShellLayer::kTop,
        .anchor = ui::kLayerShellAnchorTop | ui::kLayerShellAnchorLeft |
                  ui::kLayerShellAnchorRight,
        .width = 1920,
        .height = 32,
        .exclusive_zone = 32,
        .keyboard = ui::LayerShellKeyboardInteractivity::kNone,
        .layer_namespace = "views-shell-bar",
    },
    // The workspace strip (docs/tabs.md, --left-tabs): a panel on the left
    // edge, stretched top to bottom, 960 px wide (the strip and its content
    // area), above windows and reserving no space, never taking the keyboard.
    {
        .name = "left-tabs",
        .layer = ui::LayerShellLayer::kTop,
        .anchor = ui::kLayerShellAnchorTop | ui::kLayerShellAnchorBottom |
                  ui::kLayerShellAnchorLeft,
        .width = 960,
        .height = 540,
        .exclusive_zone = 0,
        .keyboard = ui::LayerShellKeyboardInteractivity::kNone,
        .layer_namespace = "views-shell-left-tabs",
    },
}};

bool IsTopLevelWindow(const views::Widget::InitParams& params) {
  return !params.child &&
         (params.type == views::Widget::InitParams::TYPE_WINDOW ||
          params.type == views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
}

bool SameRequest(const ui::LayerShellProperties& a,
                 const ui::LayerShellProperties& b) {
  return a.layer == b.layer && a.anchor == b.anchor &&
         a.exclusive_zone == b.exclusive_zone &&
         a.keyboard_interactivity == b.keyboard_interactivity &&
         a.margin_top == b.margin_top && a.margin_right == b.margin_right &&
         a.margin_bottom == b.margin_bottom && a.margin_left == b.margin_left &&
         a.layer_namespace == b.layer_namespace && a.output_id == b.output_id;
}

}  // namespace

base::span<const SurfaceSpec> GetSurfaceSpecs() {
  return kSurfaceSpecs;
}

const SurfaceSpec* FindSurfaceSpec(std::string_view name) {
  for (const SurfaceSpec& spec : kSurfaceSpecs) {
    if (spec.name == name) {
      return &spec;
    }
  }
  return nullptr;
}

ui::LayerShellProperties ToLayerShellProperties(const SurfaceSpec& spec) {
  ui::LayerShellProperties properties;
  properties.layer = spec.layer;
  properties.anchor = spec.anchor;
  properties.exclusive_zone = spec.exclusive_zone;
  properties.keyboard_interactivity = spec.keyboard;
  properties.layer_namespace = std::string(spec.layer_namespace);
  return properties;
}

void ApplySurfaceSpec(const SurfaceSpec& spec,
                      views::Widget::InitParams* params) {
  // Frameless: a standard frame would add its caption to the requested size.
  params->type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  params->layer_shell = ToLayerShellProperties(spec);
  params->bounds = gfx::Rect(0, 0, spec.width, spec.height);
}

void CheckSurfaceSpec(const views::Widget::InitParams& params) {
  if (!IsTopLevelWindow(params)) {
    return;
  }
  CHECK(params.layer_shell.has_value())
      << "rule R1: views-shell never creates an xdg_toplevel; top-level widget '"
      << params.name << "' has no layer-shell request (use ApplySurfaceSpec)";
  const ui::LayerShellProperties& request = *params.layer_shell;
  const SurfaceSpec* spec = nullptr;
  for (const SurfaceSpec& candidate : kSurfaceSpecs) {
    if (candidate.layer_namespace == request.layer_namespace) {
      spec = &candidate;
    }
  }
  CHECK(spec) << "rule R1: layer surface namespace '" << request.layer_namespace
              << "' is not a registered SurfaceSpec (app/surface_spec.cc)";
  CHECK(SameRequest(request, ToLayerShellProperties(*spec)))
      << "rule R1: widget '" << params.name << "' asks for namespace '"
      << request.layer_namespace << "' with a request that differs from the "
      << "registered SurfaceSpec '" << spec->name << "'";
}

}  // namespace views_shell
