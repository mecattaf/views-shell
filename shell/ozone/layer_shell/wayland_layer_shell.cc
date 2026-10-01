// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// agency Ozone Patch: Layer-shell global binding implementation.
// Chromium target path: ui/ozone/platform/wayland/host/wayland_layer_shell.cc
//
// LIFTED VERBATIM (logic-faithful) from the proven cowl substrate:
//   cowl/patches/ozone/wayland_layer_shell.cc
// See PROVENANCE.md in this directory.
//
// Binds zwlr_layer_shell_v1 from the Wayland registry using the
// GlobalObjectRegistrar<T> pattern. The bound version is clamped to
// kMaxVersion (5) to ensure forward compatibility.

#include "ui/ozone/platform/wayland/host/wayland_layer_shell.h"

#include "cowl/build/cowl_buildflags.h"

#if BUILDFLAG(ENABLE_COWL_LAYER_SHELL)

// 'namespace' is a C++ keyword but used as a parameter name in the
// wlr-layer-shell protocol XML.  Work around the generated C header.
#define namespace namespace_
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#undef namespace

#include <algorithm>

#include "base/check_op.h"
#include "base/logging.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"

namespace ui {

// static
constexpr char WaylandLayerShell::kInterfaceName[];

// static
void WaylandLayerShell::Instantiate(WaylandConnection* connection,
                                    wl_registry* registry,
                                    uint32_t name,
                                    const std::string& interface,
                                    uint32_t version) {
  CHECK_EQ(interface, kInterfaceName);

  if (connection->layer_shell_ ||
      !wl::CanBind(interface, version, kMinVersion, kMaxVersion)) {
    return;
  }

  auto bound_version = std::min(version, kMaxVersion);
  auto layer_shell =
      wl::Bind<zwlr_layer_shell_v1>(registry, name, bound_version);
  if (!layer_shell) {
    LOG(ERROR) << "Failed to bind zwlr_layer_shell_v1";
    return;
  }

  connection->layer_shell_ = std::make_unique<WaylandLayerShell>(
      layer_shell.release(), connection);
}

WaylandLayerShell::WaylandLayerShell(zwlr_layer_shell_v1* layer_shell,
                                     WaylandConnection* connection)
    : layer_shell_(layer_shell),
      version_(zwlr_layer_shell_v1_get_version(layer_shell_.get())) {
  VLOG(1) << "Bound zwlr_layer_shell_v1 version " << version_;
}

WaylandLayerShell::~WaylandLayerShell() = default;

}  // namespace ui

#endif  // BUILDFLAG(ENABLE_COWL_LAYER_SHELL)
