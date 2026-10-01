// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cowl/src/cowl_browser_main_parts.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/client/focus_change_observer.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/env.h"
#include "ui/aura/screen_ozone.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/wm/core/cursor_loader.h"
#include "ui/wm/core/default_activation_client.h"
#include "url/gurl.h"

#include "cowl/src/cowl_browser_context.h"
#include "cowl/src/cowl_content_browser_client.h"
#include "cowl/src/cowl_devtools_manager_delegate.h"
#include "cowl/src/cowl_surface_manager.h"
#include "cowl/src/cowl_web_contents_delegate.h"

namespace cowl {

namespace {

// Production FocusClient implementation.
// Cannot use aura::test::TestFocusClient (testonly = true).
class CowlFocusClient : public aura::client::FocusClient,
                         public aura::WindowObserver {
 public:
  CowlFocusClient() = default;
  ~CowlFocusClient() override = default;

  CowlFocusClient(const CowlFocusClient&) = delete;
  CowlFocusClient& operator=(const CowlFocusClient&) = delete;

  void InstallOn(aura::Window* root_window) {
    aura::client::SetFocusClient(root_window, this);
    root_windows_.push_back(root_window);
  }

  void UninstallAll() {
    for (aura::Window* root : root_windows_) {
      aura::client::SetFocusClient(root, nullptr);
    }
    root_windows_.clear();
  }

 private:
  void AddObserver(aura::client::FocusChangeObserver* observer) override {
    focus_observers_.AddObserver(observer);
  }
  void RemoveObserver(aura::client::FocusChangeObserver* observer) override {
    focus_observers_.RemoveObserver(observer);
  }
  void FocusWindow(aura::Window* window) override {
    if (window && !window->CanFocus())
      return;
    if (focused_window_)
      observation_.Reset();
    aura::Window* old_focused = focused_window_;
    focused_window_ = window;
    if (focused_window_)
      observation_.Observe(focused_window_.get());
    focus_observers_.Notify(
        &aura::client::FocusChangeObserver::OnWindowFocused,
        focused_window_, old_focused);
    aura::client::FocusChangeObserver* observer =
        aura::client::GetFocusChangeObserver(old_focused);
    if (observer)
      observer->OnWindowFocused(focused_window_, old_focused);
    observer = aura::client::GetFocusChangeObserver(focused_window_);
    if (observer)
      observer->OnWindowFocused(focused_window_, old_focused);
  }
  void ResetFocusWithinActiveWindow(aura::Window* window) override {
    if (!window->Contains(focused_window_))
      FocusWindow(window);
  }
  aura::Window* GetFocusedWindow() override { return focused_window_; }
  void OnWindowDestroying(aura::Window* window) override {
    DCHECK_EQ(window, focused_window_);
    FocusWindow(nullptr);
  }

  std::vector<raw_ptr<aura::Window>> root_windows_;
  raw_ptr<aura::Window> focused_window_ = nullptr;
  base::ScopedObservation<aura::Window, aura::WindowObserver> observation_{
      this};
  base::ObserverList<aura::client::FocusChangeObserver> focus_observers_;
};

// Production WindowParentingClient implementation.
// Cannot use aura::test::TestWindowParentingClient (testonly = true).
class CowlWindowParentingClient : public aura::client::WindowParentingClient {
 public:
  CowlWindowParentingClient() = default;
  ~CowlWindowParentingClient() override {
    UninstallAll();
  }

  CowlWindowParentingClient(const CowlWindowParentingClient&) = delete;
  CowlWindowParentingClient& operator=(const CowlWindowParentingClient&) =
      delete;

  void InstallOn(aura::Window* root_window) {
    aura::client::SetWindowParentingClient(root_window, this);
    root_windows_.push_back(root_window);
  }

  void UninstallAll() {
    for (aura::Window* root : root_windows_) {
      aura::client::SetWindowParentingClient(root, nullptr);
    }
    root_windows_.clear();
  }

 private:
  aura::Window* GetDefaultParent(aura::Window* window,
                                 const gfx::Rect& bounds,
                                 const int64_t display_id) override {
    if (!root_windows_.empty())
      return root_windows_.front();
    return nullptr;
  }

  std::vector<raw_ptr<aura::Window>> root_windows_;
};

}  // namespace

CowlBrowserMainParts::CowlBrowserMainParts(
    CowlContentBrowserClient* browser_client)
    : browser_client_(browser_client) {}

CowlBrowserMainParts::~CowlBrowserMainParts() = default;

int CowlBrowserMainParts::PreEarlyInitialization() {
  return 0;
}

int CowlBrowserMainParts::PreMainMessageLoopRun() {
  // Step 1: Create browser context.
  browser_client_->set_browser_context(
      std::make_unique<CowlBrowserContext>());

  // Step 2: Initialize shared Aura/Ozone infrastructure.
  InitializeAuraInfrastructure();

  // Step 3: Create surface manager.
  surface_manager_ = std::make_unique<CowlSurfaceManager>(
      browser_client_->browser_context(), browser_client_);

  // Step 4: Parse command line for surface types and URL.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  GURL url("about:blank");
  if (command_line->HasSwitch("url")) {
    url = GURL(command_line->GetSwitchValueASCII("url"));
    if (!url.is_valid()) {
      LOG(ERROR) << "Invalid --url value, falling back to about:blank";
      url = GURL("about:blank");
    }
  } else {
    const auto& args = command_line->GetArgs();
    if (!args.empty()) {
      GURL arg_url(args[0]);
      if (arg_url.is_valid() && arg_url.has_scheme())
        url = arg_url;
    }
  }

  // Step 5: Parse surface types and create surfaces.
  std::vector<std::string> surface_types = ParseSurfaceTypes();

  if (surface_types.empty()) {
    CowlSurfaceManager::SurfaceConfig config;
    config.id = "default";
    config.surface_type = "";
    config.url = url.spec();
    surface_manager_->CreateSurface(config);
  } else {
    for (const auto& surface_type : surface_types) {
      CowlSurfaceManager::SurfaceConfig config;
      config.id = surface_type;
      config.surface_type = surface_type;

      if (surface_types.size() == 1 && url.spec() != "about:blank") {
        config.url = url.spec();
      } else {
        config.url = "os://" + surface_type + "/";
      }

      if (!surface_manager_->CreateSurface(config)) {
        LOG(ERROR) << "COWL: Failed to create surface '" << surface_type << "'";
      }
    }
  }

  // Step 6: Install Aura clients on each surface's root window. Native-Views
  // surfaces have no WebContents, so install on the host root directly and then
  // show+focus them (deferred until the activation/focus clients exist, since
  // views::Widget::Show() activates the widget).
  for (const auto& id : surface_manager_->GetSurfaceIds()) {
    content::WebContents* wc = surface_manager_->GetSurface(id);
    if (wc) {
      InstallAuraClientsOnSurface(wc);
    } else if (aura::WindowTreeHost* host =
                   surface_manager_->GetSurfaceHost(id)) {
      InstallAuraClientsOnRoot(host->window());
      surface_manager_->ShowViewsSurface(id);
    }
  }

  // Step 7: Start DevTools HTTP server.
  int devtools_port = 0;
  if (command_line->HasSwitch("devtools-port")) {
    int temp_port;
    if (base::StringToInt(
            command_line->GetSwitchValueASCII("devtools-port"),
            &temp_port) &&
        temp_port > 0 && temp_port < 65535) {
      devtools_port = temp_port;
    }
  }

  CowlDevToolsManagerDelegate::StartHttpHandler(
      browser_client_->browser_context(), devtools_port);

  DVLOG(1) << "COWL: Started with " << surface_manager_->SurfaceCount()
            << " surface(s)";
  return 0;
}

void CowlBrowserMainParts::InstallAuraClientsOnSurface(
    content::WebContents* wc) {
  if (!wc || !wc->GetNativeView())
    return;
  InstallAuraClientsOnRoot(wc->GetNativeView()->GetRootWindow());
}

void CowlBrowserMainParts::InstallAuraClientsOnRoot(aura::Window* root) {
  if (!root)
    return;

  static_cast<CowlFocusClient*>(focus_client_.get())->InstallOn(root);
  static_cast<CowlWindowParentingClient*>(
      window_parenting_client_.get())->InstallOn(root);

  // DefaultActivationClient self-installs on root (raw new, matches
  // content_shell pattern).
  new wm::DefaultActivationClient(root);

  capture_clients_.push_back(
      std::make_unique<aura::client::DefaultCaptureClient>(root));
}

void CowlBrowserMainParts::InitializeAuraInfrastructure() {
  CHECK(aura::Env::GetInstance());

  if (!display::Screen::HasScreen()) {
    screen_ = std::make_unique<aura::ScreenOzone>();
  }

  focus_client_ = std::make_unique<CowlFocusClient>();
  window_parenting_client_ = std::make_unique<CowlWindowParentingClient>();

  cursor_shape_client_ = std::make_unique<wm::CursorLoader>();
  aura::client::SetCursorShapeClient(cursor_shape_client_.get());
}

void CowlBrowserMainParts::TearDownAuraInfrastructure() {
  static_cast<CowlFocusClient*>(focus_client_.get())->UninstallAll();
  static_cast<CowlWindowParentingClient*>(
      window_parenting_client_.get())->UninstallAll();

  aura::client::SetCursorShapeClient(nullptr);
  cursor_shape_client_.reset();
  window_parenting_client_.reset();
  capture_clients_.clear();
  focus_client_.reset();
  screen_.reset();
}

std::vector<std::string> CowlBrowserMainParts::ParseSurfaceTypes() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::vector<std::string> surface_types;

  if (command_line->HasSwitch("surfaces")) {
    std::string surfaces_str =
        command_line->GetSwitchValueASCII("surfaces");
    surface_types = base::SplitString(
        surfaces_str, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  } else if (command_line->HasSwitch("surface")) {
    std::string surface_type =
        command_line->GetSwitchValueASCII("surface");
    if (!surface_type.empty()) {
      surface_types.push_back(surface_type);
    }
  }

  return surface_types;
}

void CowlBrowserMainParts::PostMainMessageLoopRun() {
  // Tear down in reverse order of creation.
  surface_manager_->DestroyAllSurfaces();
  surface_manager_.reset();

  TearDownAuraInfrastructure();

  CowlDevToolsManagerDelegate::StopHttpHandler();

  browser_client_->set_browser_context(nullptr);
}

}  // namespace cowl
