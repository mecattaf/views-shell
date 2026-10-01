// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cowl/src/cowl_surface_manager.h"

#include "base/logging.h"
#include "build/buildflag.h"
#include "cowl/build/cowl_buildflags.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_platform.h"
#include "ui/compositor/compositor.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/ozone/platform/wayland/host/wayland_layer_shell_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#include "cowl/src/cowl_content_browser_client.h"
#include "cowl/src/cowl_web_contents_delegate.h"
#include "cowl/src/views_shell_bar.h"
#include "cowl/src/views_shell_launcher_panel.h"
#include "cowl/src/os_service_impl.h"

namespace cowl {

// Static global instance for OsServiceImpl routing.
CowlSurfaceManager* CowlSurfaceManager::g_instance = nullptr;

namespace {

// Layout manager that fills child windows to the root window size.
class FillLayout : public aura::LayoutManager {
 public:
  explicit FillLayout(aura::Window* root) : root_(root) {}
  ~FillLayout() override = default;

  FillLayout(const FillLayout&) = delete;
  FillLayout& operator=(const FillLayout&) = delete;

 private:
  void OnWindowResized() override {
    // Always propagate -- no one-shot gate.
    for (aura::Window* child : root_->children())
      SetChildBoundsDirect(child, gfx::Rect(root_->bounds().size()));
  }
  void OnWindowAddedToLayout(aura::Window* child) override {
    // Use size-only rect (origin 0,0), not screen-position rect.
    child->SetBounds(gfx::Rect(root_->bounds().size()));
  }
  void OnWillRemoveWindowFromLayout(aura::Window* child) override {}
  void OnWindowRemovedFromLayout(aura::Window* child) override {}
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override {}
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override {
    SetChildBoundsDirect(child, requested_bounds);
  }

  raw_ptr<aura::Window> root_;
};

// Surface types that float over the desktop and therefore need a transparent
// base. Without this their WebContents paints an opaque base color over the
// whole surface rectangle -- e.g. the full-height `bar` surface would cover
// the windows beneath it everywhere its CSS is transparent (below the strip).
// Opaque surfaces (ccd sidebar, lock) intentionally paint their own bg in CSS.
bool SurfaceTypeIsTransparent(const std::string& type) {
  return type == "bar" || type == "dock" || type == "osd" ||
         type == "notifications" || type == "overlay" || type == "ccd" ||
         type == "launcher" || type == "clui" || type == "viewspike" ||
         type == "viewsbar";
}

// Minimal concrete ViewsDelegate so the //ui/views stack can bootstrap inside
// the cowl browser process (its constructor self-registers as the singleton).
// cowl has never instantiated Views before; this is all Views needs to run.
class ShellViewsDelegate : public views::ViewsDelegate {
 public:
  ShellViewsDelegate() = default;
  ~ShellViewsDelegate() override = default;
};

void EnsureViewsEnv() {
  // Both ctors self-register as their process-wide singleton; intentionally
  // leaked for the spike's lifetime (nothing needs ordered teardown). The
  // LayoutProvider is mandatory: views::Textfield (and most controls) deref
  // LayoutProvider::Get() during construction.
  if (!views::ViewsDelegate::GetInstance())
    new ShellViewsDelegate();
  if (!views::LayoutProvider::Get())
    new views::LayoutProvider();
}

// Primary output width in logical px (fallback if the screen isn't up yet).
int OutputWidth() {
  if (display::Screen::HasScreen()) {
    return display::Screen::Get()->GetPrimaryDisplay().size().width();
  }
  return 2560;
}

// The two ccd sidebar widths, both proportional to the output so they're
// scale-correct (a hardcoded px count is wrong on non-2560 outputs). Tuning is
// a one-line divisor change.
//   Full     = output / 8  = 12.5% (the expanded sidebar).
//   Collapsed= output / kCcdRailDivisor (~5%, ~128px @2560) -- the rail.
// 36px (= kBarHeight, bar-thickness symmetry) was the first rail width but read
// as too cramped for the workspace badges, so the rail was widened to ~5%. For
// the 36px bar-symmetry rail instead, return a fixed 36 from CcdCollapsedWidth().
constexpr int kCcdRailDivisor = 20;  // ~5% of output width
int CcdFullWidth() { return OutputWidth() / 8; }
int CcdCollapsedWidth() { return OutputWidth() / kCcdRailDivisor; }

// Configure PlatformWindowInitProperties for the given surface type.
// Returns true if layer-shell properties were applied.
bool ConfigureLayerShellProperties(
    ui::PlatformWindowInitProperties& properties,
    const std::string& surface_type,
    const gfx::Size& size) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  if (surface_type == "bar") {
    // Top bar. Keep the SURFACE full height (config default ~1080) so widget
    // popouts (wifi/audio/etc.) can render *below* the bar strip without being
    // clipped, but reserve only the bar height via the exclusive zone so tiled
    // windows sit just under the bar and the bar stays in place. The bar body
    // is transparent below the strip, so the rest shows the desktop through.
    constexpr int kBarHeight = 36;  // --bar-height (32) + a little breathing room
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kTop;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = kBarHeight;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kOnDemand;
    properties.layer_shell_namespace = "cowl-bar";
    // Pin the surface to the strip height: a thin frosted bar that doesn't
    // overlap (and blur) the windows below it. (Popouts can't bleed below a
    // strip-height surface -- that's the tradeoff for the clean frosted look.)
    properties.bounds = gfx::Rect(size.width(), kBarHeight);
    return true;
  }
  if (surface_type == "overlay") {
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = 0;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kOnDemand;
    properties.layer_shell_namespace = "cowl-overlay";
    return true;
  }
  if (surface_type == "launcher") {
    // Raycast/Spotlight launcher: a full-screen TRANSPARENT overlay; the web
    // layer (surfaces/launcher) paints a centered floating panel and centers it
    // with CSS. Anchoring all four edges makes niri size the surface to the full
    // output so the panel can be CSS-centered and outside-clicks land on the
    // surface. Overlay layer => above bar/ccd (top) and above niri's overview.
    // kExclusive keyboard: niri routes keyboard focus to the overlay the moment
    // it maps (like rofi/wofi), so you can type immediately on summon without
    // first clicking in. The grab releases when the surface is hidden, and Esc
    // still reaches the surface so it can self-dismiss.
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = 0;  // floats; reserves no space
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kExclusive;
    properties.layer_shell_namespace = "cowl-launcher";
    return true;
  }
  if (surface_type == "viewspike") {
    // The Views-on-layer-shell spike. Same geometry as the (web) launcher: a
    // full-screen transparent overlay whose content is a CENTERED floating
    // panel -- except the content is a native C++ Views tree, not a WebContents.
    // kExclusive keyboard => niri routes keyboard focus the instant it maps, so
    // the search Textfield can take input immediately on summon (signal #4).
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = 0;  // floats; reserves no space
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kExclusive;
    properties.layer_shell_namespace = "cowl-viewspike";
    return true;
  }
  if (surface_type == "viewsbar") {
    // The Views top bar -- same geometry as the (web) `bar`: a frosted
    // full-width strip anchored Top|Left|Right with an exclusive zone so tiled
    // windows sit just below it. kOnDemand keyboard (click-to-focus), unlike
    // the launcher's kExclusive grab -- a bar must not steal the keyboard.
    constexpr int kBarHeight = 36;
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kTop;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = kBarHeight;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kOnDemand;
    properties.layer_shell_namespace = "cowl-viewsbar";
    properties.bounds = gfx::Rect(size.width(), kBarHeight);
    return true;
  }
  if (surface_type == "clui") {
    // clui-cc port: a full-screen TRANSPARENT overlay whose web layer paints a
    // floating Claude-Code conversation panel (see surfaces/clui). Same shape as
    // the launcher/overlay: all-edge anchored, overlay layer, kExclusive keyboard
    // so niri routes focus the moment it maps and the input bar can type
    // immediately on summon (grab releases on hide; Esc still self-dismisses).
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = 0;  // floats; reserves no space
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kExclusive;
    properties.layer_shell_namespace = "cowl-clui";
    return true;
  }
  if (surface_type == "osd") {
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorBottom;
    properties.layer_shell_exclusive_zone = 0;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kNone;
    properties.layer_shell_namespace = "cowl-osd";
    return true;
  }
  if (surface_type == "notifications") {
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kTop;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = 0;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kNone;
    properties.layer_shell_namespace = "cowl-notifications";
    return true;
  }
  if (surface_type == "dock") {
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kTop;
    properties.layer_shell_anchor = ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = size.height();
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kNone;
    properties.layer_shell_namespace = "cowl-dock";
    return true;
  }
  if (surface_type == "ccd") {
    // Left-edge sidebar at 12.5% of the screen width (1/8) -- proportional, not
    // a hardcoded pixel count. The collapse toggle (SetCcdCollapsed) flips this
    // between CcdFullWidth() and CcdCollapsedWidth() at runtime.
    const int kCcdWidth = CcdFullWidth();
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kTop;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft;
    properties.layer_shell_exclusive_zone = kCcdWidth;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kOnDemand;
    properties.layer_shell_namespace = "cowl-ccd";
    // Anchored top+bottom => height spans the output; width is taken from the
    // requested size, so pin it to the sidebar width (default config is
    // 1920x1080, which would otherwise fill the screen).
    properties.bounds = gfx::Rect(kCcdWidth, size.height());
    return true;
  }
  if (surface_type == "lock") {
    properties.type = ui::PlatformWindowType::kLayerShell;
    properties.layer_shell_layer = ui::LayerShellLayer::kOverlay;
    properties.layer_shell_anchor = ui::kLayerShellAnchorTop |
                                    ui::kLayerShellAnchorBottom |
                                    ui::kLayerShellAnchorLeft |
                                    ui::kLayerShellAnchorRight;
    properties.layer_shell_exclusive_zone = -1;
    properties.layer_shell_keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kExclusive;
    properties.layer_shell_namespace = "cowl-lock";
    return true;
  }
#else
  if (surface_type == "bar" || surface_type == "overlay" ||
      surface_type == "launcher" || surface_type == "clui" ||
      surface_type == "osd" || surface_type == "notifications" ||
      surface_type == "dock" || surface_type == "lock") {
    LOG(WARNING) << "COWL: Layer-shell surface '" << surface_type
                 << "' requested but Ozone patch not applied. "
                 << "Falling back to xdg_toplevel.";
  }
#endif

  return false;
}

}  // namespace

CowlSurfaceManager::SurfaceConfig::SurfaceConfig() = default;
CowlSurfaceManager::SurfaceConfig::SurfaceConfig(const SurfaceConfig&) =
    default;
CowlSurfaceManager::SurfaceConfig& CowlSurfaceManager::SurfaceConfig::
operator=(const SurfaceConfig&) = default;
CowlSurfaceManager::SurfaceConfig::~SurfaceConfig() = default;

CowlSurfaceManager::Surface::Surface() = default;
CowlSurfaceManager::Surface::~Surface() = default;

CowlSurfaceManager::CowlSurfaceManager(
    content::BrowserContext* browser_context,
    CowlContentBrowserClient* browser_client)
    : browser_context_(browser_context),
      browser_client_(browser_client) {
  DCHECK(browser_context_);
  DCHECK(!g_instance);
  g_instance = this;
}

CowlSurfaceManager::~CowlSurfaceManager() {
  DestroyAllSurfaces();
  g_instance = nullptr;
}

// static
CowlSurfaceManager* CowlSurfaceManager::GetInstance() {
  return g_instance;
}

bool CowlSurfaceManager::CreateSurface(const SurfaceConfig& config) {
  if (config.id.empty()) {
    LOG(ERROR) << "COWL: Cannot create surface with empty id";
    return false;
  }

  if (surfaces_.contains(config.id)) {
    LOG(ERROR) << "COWL: Surface '" << config.id << "' already exists";
    return false;
  }

  // Step 1: Configure platform window properties.
  gfx::Size size(config.width, config.height);
  ui::PlatformWindowInitProperties properties;
  properties.bounds = gfx::Rect(size);

  bool is_layer_shell =
      ConfigureLayerShellProperties(properties, config.surface_type, size);

  // Surfaces that composite over the desktop need a translucent platform
  // window (ARGB buffer) so the transparent base actually shows through.
  const bool wants_transparency =
      is_layer_shell && SurfaceTypeIsTransparent(config.surface_type);
  if (wants_transparency) {
    properties.opacity = ui::PlatformWindowOpacity::kTranslucentWindow;
  }

  if (is_layer_shell) {
    DVLOG(1) << "COWL: Creating layer-shell surface '" << config.id
              << "' (type=" << config.surface_type << ")";
  } else if (!config.surface_type.empty()) {
    DVLOG(1) << "COWL: Creating tiled window for '" << config.id
              << "' (type=" << config.surface_type << ")";
  } else {
    DVLOG(1) << "COWL: Creating tiled window '" << config.id << "'";
  }

  // Step 2: Create WindowTreeHost.
  auto surface = std::make_unique<Surface>();
  surface->config = config;
  surface->host = aura::WindowTreeHost::Create(std::move(properties));
  surface->host->InitHost();
  surface->host->window()->Show();
  surface->host->window()->SetLayoutManager(
      std::make_unique<FillLayout>(surface->host->window()));

  // Native-Views surface: host a retained-mode Views tree on this layer-shell
  // surface instead of a WebContents. This is the views-shell "Views shell on
  // layer-shell under niri" weld -- the same attach point as the WebContents
  // path (a child aura::Window of host->window()), but the child is the Views
  // widget's NativeWidgetAura window. FillLayout resizes it to the surface as
  // the compositor configures the real output size.
  //   viewspike = the launcher (full-screen transparent overlay, centered panel)
  //   viewsbar  = the top bar (full-width frosted strip)
  const bool is_views_surface = config.surface_type == "viewspike" ||
                                config.surface_type == "viewsbar";
  if (is_views_surface) {
    EnsureViewsEnv();

    aura::Window* root = surface->host->window();
    auto widget = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.parent = root;    // child window in the EXISTING host's aura tree
    params.context = root;
    params.bounds = gfx::Rect(root->bounds().size());
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.activatable = views::Widget::InitParams::Activatable::kYes;
    widget->Init(std::move(params));

    // Compute the blur region per surface type, and set the contents view.
    int out_w = config.width, out_h = config.height;
    if (display::Screen::HasScreen()) {
      gfx::Size s = display::Screen::Get()->GetPrimaryDisplay().size();
      out_w = s.width();
      out_h = s.height();
    }
    gfx::Rect blur_rect;
    if (config.surface_type == "viewspike") {
      auto* root_view =
          widget->SetContentsView(std::make_unique<ShellLauncherRoot>());
      // Centered panel: blur the panel rect (matches the BoxLayout centering).
      gfx::Size pnl = root_view->panel()->GetPreferredSize();
      blur_rect = gfx::Rect((out_w - pnl.width()) / 2,
                            (out_h - pnl.height()) / 2, pnl.width(),
                            pnl.height());
    } else {  // viewsbar
      widget->SetContentsView(std::make_unique<ShellBarRoot>());
      // Frost the whole bar strip (height matches the viewsbar exclusive zone).
      blur_rect = gfx::Rect(0, 0, out_w, 36);
    }

    // Transparent compositor base: only the panel/bar paints; the rest of the
    // surface shows the desktop through.
    if (surface->host->compositor()) {
      surface->host->compositor()->SetBackgroundColor(SK_ColorTRANSPARENT);
    }

    surface->host->Show();
    // NOTE: widget->Show() (+ search focus for the launcher) is DEFERRED to
    // ShowViewsSurface(), invoked after the activation/focus aura clients are
    // installed on this host root. views::Widget::Show() activates the widget,
    // which null-derefs wm::GetActivationClient(root) if no client exists yet.

    surface->widget = std::move(widget);
    surfaces_[config.id] = std::move(surface);
    SetBlurRegion(config.id, {blur_rect});
    DVLOG(1) << "COWL: Views surface '" << config.id
             << "' (type=" << config.surface_type << ") mapped";
    return true;
  }

  // Step 3: Create WebContents.
  content::WebContents::CreateParams create_params(browser_context_);
  surface->web_contents = content::WebContents::Create(create_params);

  // Step 4: Attach delegate + observer.
  surface->delegate = std::make_unique<CowlWebContentsDelegate>(
      surface->web_contents.get(), this);
  surface->web_contents->SetDelegate(surface->delegate.get());

  // Step 5: Attach WebContents native view to the Aura window tree.
  aura::Window* content_window = surface->web_contents->GetNativeView();
  aura::Window* parent_window = surface->host->window();
  if (!parent_window->Contains(content_window)) {
    parent_window->AddChild(content_window);
  }
  content_window->Show();

  // Make floating surfaces (bar/dock/osd/notifications/overlay) transparent so
  // they don't paint an opaque base over windows below them. The WebContents
  // base background, the aura window, and the compositor all need to agree.
  if (wants_transparency) {
    content_window->SetTransparent(true);
    surface->web_contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
    if (surface->host->compositor()) {
      surface->host->compositor()->SetBackgroundColor(SK_ColorTRANSPARENT);
    }
  }

  // Step 6: Show the platform window.
  surface->host->Show();

  // Step 7: Navigate to the URL.
  GURL url(config.url);
  if (url.is_valid() && !url.is_empty() && url != GURL("about:blank")) {
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_TYPED;
    surface->web_contents->GetController().LoadURLWithParams(params);
    DVLOG(1) << "COWL: Surface '" << config.id << "' navigating to "
              << url.spec();
  }

  surfaces_[config.id] = std::move(surface);
  return true;
}

bool CowlSurfaceManager::ShowViewsSurface(const std::string& id) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end() || !it->second->widget)
    return false;

  views::Widget* widget = it->second->widget.get();
  widget->Show();

  // The launcher pulls keyboard focus into its search field -- the signal #4
  // go/no-go. With kExclusive, niri has already routed the keyboard to the
  // surface; this asks Views' FocusManager to land it in the Textfield. The bar
  // is kOnDemand (click-to-focus) and has no search field, so it just shows.
  if (it->second->config.surface_type == "viewspike") {
    auto* root_view = static_cast<ShellLauncherRoot*>(widget->GetContentsView());
    if (root_view && root_view->panel())
      root_view->panel()->search()->RequestFocus();
  }
  DVLOG(1) << "COWL: Views surface '" << id << "' shown";
  return true;
}

bool CowlSurfaceManager::ShowSurface(const std::string& id) {
  auto it = surfaces_.find(id);
  if (it != surfaces_.end()) {
    // Surface exists -- show it if hidden.
    Surface* surface = it->second.get();
    if (!surface->visible) {
      surface->host->Show();
      surface->visible = true;
      DVLOG(1) << "COWL: Showing surface '" << id << "'";
    }
    return true;
  }

  // Surface doesn't exist -- create it with default config.
  SurfaceConfig config;
  config.id = id;
  config.surface_type = id;
  config.url = "os://" + id + "/";
  return CreateSurface(config);
}

bool CowlSurfaceManager::HideSurface(const std::string& id) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end()) {
    LOG(WARNING) << "COWL: Cannot hide surface '" << id << "': not found";
    return false;
  }

  Surface* surface = it->second.get();
  if (surface->visible) {
    surface->host->Hide();
    surface->visible = false;
    DVLOG(1) << "COWL: Hiding surface '" << id << "'";
  }
  return true;
}

bool CowlSurfaceManager::HasSurface(const std::string& id) const {
  return surfaces_.contains(id);
}

void CowlSurfaceManager::DestroySurface(const std::string& id) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end()) {
    LOG(WARNING) << "COWL: Cannot destroy surface '" << id << "': not found";
    return;
  }

  DVLOG(1) << "COWL: Destroying surface '" << id << "'";

  // Tear down in reverse order of creation. Native-Views surfaces have a
  // widget and no WebContents; web surfaces have the reverse.
  Surface* surface = it->second.get();
  if (surface->widget) {
    surface->widget.reset();  // destroys the child NativeWidgetAura window
  }
  if (surface->web_contents) {
    surface->web_contents->SetDelegate(nullptr);
    surface->delegate.reset();
    surface->web_contents.reset();
  }
  surface->host.reset();

  surfaces_.erase(it);
}

void CowlSurfaceManager::DestroyAllSurfaces() {
  std::vector<std::string> ids;
  ids.reserve(surfaces_.size());
  for (const auto& [id, _] : surfaces_) {
    ids.push_back(id);
  }
  for (const auto& id : ids) {
    DestroySurface(id);
  }
}

content::WebContents* CowlSurfaceManager::GetSurface(
    const std::string& id) const {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end())
    return nullptr;
  return it->second->web_contents.get();
}

std::vector<std::string> CowlSurfaceManager::GetSurfaceIds() const {
  std::vector<std::string> ids;
  ids.reserve(surfaces_.size());
  for (const auto& [id, _] : surfaces_) {
    ids.push_back(id);
  }
  return ids;
}

size_t CowlSurfaceManager::SurfaceCount() const {
  return surfaces_.size();
}

aura::WindowTreeHost* CowlSurfaceManager::GetSurfaceHost(
    const std::string& id) const {
  return GetHost(id);
}

aura::WindowTreeHost* CowlSurfaceManager::GetHost(
    const std::string& id) const {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end())
    return nullptr;
  return it->second->host.get();
}

bool CowlSurfaceManager::SetLayer(const std::string& id, uint32_t layer) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;
  pw->SetLayer(static_cast<ui::LayerShellLayer>(layer));
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetAnchor(const std::string& id, uint32_t anchor) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;
  pw->SetAnchor(anchor);
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetExclusiveZone(const std::string& id,
                                           int32_t zone) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;
  pw->SetExclusiveZone(zone);
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetKeyboardInteractivity(const std::string& id,
                                                   uint32_t mode) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;
  pw->SetKeyboardInteractivity(
      static_cast<ui::LayerShellKeyboardInteractivity>(mode));
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetSurfaceSize(const std::string& id,
                                         int32_t width, int32_t height) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  host->SetBoundsInPixels(gfx::Rect(width, height));
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetSurfaceWidth(const std::string& id,
                                          int32_t width) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;

  // |width| is the same integer space creation uses: CreateSurface() sets
  // properties.bounds = Rect(kCcdWidth, h) AND exclusive_zone = kCcdWidth with
  // the SAME value (no device-scale multiply), and that renders correctly. So we
  // mirror it: pass |width| straight to both the bounds and the exclusive zone.
  // (Verified live: setSurfaceSize(W)/SetBoundsInPixels(W) yields a W-CSS-px-wide
  // surface; scaling by device_scale_factor double-sized it.) Keep the current
  // height -- the ccd is anchored top+bottom, so the compositor decides height
  // and we only change the unanchored (left) axis; reading it back round-trips
  // regardless of unit interpretation.
  const int height_keep = host->GetBoundsInPixels().height();

  // Order: resize the surface, then publish the new exclusive zone so niri
  // re-tiles. Both requests are double-buffered and flushed by the resize's
  // commit cycle.
  host->SetBoundsInPixels(gfx::Rect(width, height_keep));
  pw->SetExclusiveZone(width);
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetCcdCollapsed(bool collapsed) {
  // Semantic ccd collapse: cowl owns both widths (full = CcdFullWidth() = 12.5%;
  // collapsed = CcdCollapsedWidth() = ~5% rail). Built on the generic
  // SetSurfaceWidth primitive so the resize + exclusive-zone + re-tile behave
  // identically. Tuning the rail width is the one-line kCcdRailDivisor.
  return SetSurfaceWidth("ccd", collapsed ? CcdCollapsedWidth() : CcdFullWidth());
}

bool CowlSurfaceManager::SetMargin(const std::string& id,
                                    int32_t top, int32_t right,
                                    int32_t bottom, int32_t left) {
#if BUILDFLAG(COWL_HAS_LAYER_SHELL)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;
  pw->SetMargin(top, right, bottom, left);
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SetBlurRegion(
    const std::string& id,
    const std::vector<gfx::Rect>& rects) {
#if BUILDFLAG(COWL_HAS_BACKGROUND_EFFECT)
  aura::WindowTreeHost* host = GetHost(id);
  if (!host)
    return false;
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  auto* pw = static_cast<ui::WaylandLayerShellWindow*>(
      host_platform->platform_window());
  if (!pw)
    return false;

  // Apply DPI scaling: CSS pixels -> device pixels.
  float scale = host->device_scale_factor();
  std::vector<gfx::Rect> scaled_rects;
  scaled_rects.reserve(rects.size());
  for (const auto& r : rects) {
    scaled_rects.emplace_back(
        static_cast<int>(r.x() * scale),
        static_cast<int>(r.y() * scale),
        static_cast<int>(r.width() * scale),
        static_cast<int>(r.height() * scale));
  }

  pw->SetBlurRegion(
      std::optional<std::vector<gfx::Rect>>(std::move(scaled_rects)));
  return true;
#else
  return false;
#endif
}

bool CowlSurfaceManager::SendMessage(const std::string& source_surface_id,
                                     const std::string& target_surface_id,
                                     const std::string& data) {
  auto it = surfaces_.find(target_surface_id);
  if (it == surfaces_.end()) {
    DVLOG(1) << "COWL: SendMessage target '" << target_surface_id
             << "' not found";
    return false;
  }

  content::WebContents* target_wc = it->second->web_contents.get();
  if (!target_wc)
    return false;

  content::RenderFrameHost* rfh = target_wc->GetPrimaryMainFrame();
  if (!rfh)
    return false;

  // Route through the Mojo observer. The renderer-side OnMessage handler
  // constructs the CustomEvent via native V8 API.
  OsServiceImpl* target_impl = OsServiceImpl::ForRenderFrameHost(rfh);
  if (target_impl) {
    target_impl->DispatchMessage(source_surface_id, data);
    return true;
  }

  DVLOG(1) << "COWL: No OsServiceImpl for target '" << target_surface_id
           << "'";
  return false;
}

}  // namespace cowl
