// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COWL_SRC_COWL_BROWSER_MAIN_PARTS_H_
#define COWL_SRC_COWL_BROWSER_MAIN_PARTS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/browser_main_parts.h"

namespace content {
class WebContents;
}  // namespace content

namespace aura {
class ScreenOzone;
class Window;
namespace client {
class CursorShapeClient;
class DefaultCaptureClient;
class FocusClient;
class WindowParentingClient;
}  // namespace client
}  // namespace aura

namespace cowl {

class CowlContentBrowserClient;
class CowlSurfaceManager;

class CowlBrowserMainParts : public content::BrowserMainParts {
 public:
  explicit CowlBrowserMainParts(CowlContentBrowserClient* browser_client);
  ~CowlBrowserMainParts() override;

  CowlBrowserMainParts(const CowlBrowserMainParts&) = delete;
  CowlBrowserMainParts& operator=(const CowlBrowserMainParts&) = delete;

  // content::BrowserMainParts:
  int PreEarlyInitialization() override;
  int PreMainMessageLoopRun() override;
  void PostMainMessageLoopRun() override;

  CowlSurfaceManager* surface_manager() { return surface_manager_.get(); }

 private:
  void InitializeAuraInfrastructure();
  void TearDownAuraInfrastructure();
  void InstallAuraClientsOnSurface(content::WebContents* wc);
  // Install the same client stack on an arbitrary host root window (used for
  // native-Views surfaces, which have no WebContents to derive the root from).
  void InstallAuraClientsOnRoot(aura::Window* root);
  std::vector<std::string> ParseSurfaceTypes();

  raw_ptr<CowlContentBrowserClient> browser_client_;

  // Shared Aura/Ozone infrastructure
  std::unique_ptr<aura::ScreenOzone> screen_;
  std::unique_ptr<aura::client::FocusClient> focus_client_;
  std::vector<std::unique_ptr<aura::client::DefaultCaptureClient>>
      capture_clients_;
  std::unique_ptr<aura::client::WindowParentingClient> window_parenting_client_;
  std::unique_ptr<aura::client::CursorShapeClient> cursor_shape_client_;

  std::unique_ptr<CowlSurfaceManager> surface_manager_;
};

}  // namespace cowl

#endif  // COWL_SRC_COWL_BROWSER_MAIN_PARTS_H_
