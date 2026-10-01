// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COWL_SRC_COWL_SURFACE_MANAGER_H_
#define COWL_SRC_COWL_SURFACE_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/rect.h"

namespace aura {
class WindowTreeHost;
}

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace views {
class Widget;
}

namespace cowl {

class CowlContentBrowserClient;
class CowlWebContentsDelegate;

class CowlSurfaceManager {
 public:
  struct SurfaceConfig {
    SurfaceConfig();
    SurfaceConfig(const SurfaceConfig&);
    SurfaceConfig& operator=(const SurfaceConfig&);
    ~SurfaceConfig();

    std::string id;
    std::string surface_type;
    std::string url;
    int width = 1920;
    int height = 1080;
  };

  CowlSurfaceManager(content::BrowserContext* browser_context,
                      CowlContentBrowserClient* browser_client);
  ~CowlSurfaceManager();

  CowlSurfaceManager(const CowlSurfaceManager&) = delete;
  CowlSurfaceManager& operator=(const CowlSurfaceManager&) = delete;

  // Create a surface with the given config. Returns true on success.
  bool CreateSurface(const SurfaceConfig& config);

  // Show a surface. If it doesn't exist, create it with default config.
  bool ShowSurface(const std::string& id);

  // Finalize a native-Views surface: Show() its widget and focus its search
  // field. Must be called AFTER the activation/focus aura clients are installed
  // on the surface's host root (Widget::Show() activates the widget).
  bool ShowViewsSurface(const std::string& id);

  // Hide a surface (unmap, keep WebContents alive for instant re-show).
  bool HideSurface(const std::string& id);

  // Check if a surface exists.
  bool HasSurface(const std::string& id) const;

  // Destroy a surface by ID.
  void DestroySurface(const std::string& id);

  // Destroy all surfaces. Called during shutdown before Aura teardown.
  void DestroyAllSurfaces();

  // Get a surface's WebContents by ID.
  content::WebContents* GetSurface(const std::string& id) const;

  // Get a surface's host window tree host by ID (e.g. to install aura clients
  // on a native-Views surface's root). Null if not found.
  aura::WindowTreeHost* GetSurfaceHost(const std::string& id) const;

  // Get all surface IDs.
  std::vector<std::string> GetSurfaceIds() const;

  // Number of active surfaces.
  size_t SurfaceCount() const;

  // Runtime layer-shell property setters. Return false if surface not found
  // or surface is not a layer-shell surface.
  bool SetLayer(const std::string& id, uint32_t layer);
  bool SetAnchor(const std::string& id, uint32_t anchor);
  bool SetExclusiveZone(const std::string& id, int32_t zone);
  bool SetKeyboardInteractivity(const std::string& id, uint32_t mode);
  bool SetSurfaceSize(const std::string& id, int32_t width, int32_t height);
  // Set a left/right-anchored sidebar's width (DIP) and exclusive zone together,
  // keeping the current (compositor-decided) height. The collapse primitive.
  bool SetSurfaceWidth(const std::string& id, int32_t width);
  // Collapse/expand the ccd sidebar between its full (12.5%) and rail (36px)
  // widths. cowl owns both values (see CcdCollapsedWidth / kCcdRailDivisor); the
  // caller only passes the desired state.
  bool SetCcdCollapsed(bool collapsed);
  bool SetMargin(const std::string& id, int32_t top, int32_t right,
                 int32_t bottom, int32_t left);
  bool SetBlurRegion(const std::string& id,
                     const std::vector<gfx::Rect>& rects);

  // Send an inter-surface message. Dispatches to the target surface's
  // renderer via the OsServiceObserver::OnMessage callback.
  bool SendMessage(const std::string& source_surface_id,
                   const std::string& target_surface_id,
                   const std::string& data);

  // Global accessor for OsServiceImpl routing.
  static CowlSurfaceManager* GetInstance();

 private:
  struct Surface {
    Surface();
    ~Surface();

    std::unique_ptr<aura::WindowTreeHost> host;
    std::unique_ptr<content::WebContents> web_contents;
    std::unique_ptr<CowlWebContentsDelegate> delegate;
    // For native-Views surfaces (e.g. "viewspike"): the client-owned Widget
    // hosting a Views tree on this layer-shell surface instead of WebContents.
    std::unique_ptr<views::Widget> widget;
    SurfaceConfig config;
    bool visible = true;
  };

  // Get the underlying platform window for a surface, cast as needed
  // for layer-shell property mutations.
  aura::WindowTreeHost* GetHost(const std::string& id) const;

  raw_ptr<content::BrowserContext> browser_context_;
  raw_ptr<CowlContentBrowserClient> browser_client_;
  std::map<std::string, std::unique_ptr<Surface>> surfaces_;

  static CowlSurfaceManager* g_instance;
};

}  // namespace cowl

#endif  // COWL_SRC_COWL_SURFACE_MANAGER_H_
