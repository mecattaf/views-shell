// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cowl/src/cowl_content_browser_client.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

#include "cowl/mojo/os.mojom.h"
#include "cowl/src/cowl_browser_context.h"
#include "cowl/src/cowl_browser_main_parts.h"
#include "cowl/src/cowl_content_client.h"
#include "cowl/src/cowl_devtools_manager_delegate.h"
#include "cowl/src/cowl_os_url_loader_factory.h"
#include "cowl/src/os_service_impl.h"

namespace cowl {

namespace {

constexpr char kDefaultSurfacePath[] = "/usr/share/cowl/surfaces";
constexpr char kSurfacePathSwitch[] = "surface-path";

base::FilePath GetSurfaceBasePath() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(kSurfacePathSwitch)) {
    return base::FilePath(cmd->GetSwitchValueASCII(kSurfacePathSwitch));
  }
  return base::FilePath(kDefaultSurfacePath);
}

}  // namespace

CowlContentBrowserClient::CowlContentBrowserClient()
    : surface_base_path_(GetSurfaceBasePath()) {
  DVLOG(1) << "COWL: Surface base path: " << surface_base_path_.value();
}

CowlContentBrowserClient::~CowlContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
CowlContentBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  auto parts = std::make_unique<CowlBrowserMainParts>(this);
  browser_main_parts_ = parts.get();
  return parts;
}

std::string CowlContentBrowserClient::GetUserAgent() {
  return "COWL/1.0";
}

std::unique_ptr<content::DevToolsManagerDelegate>
CowlContentBrowserClient::CreateDevToolsManagerDelegate() {
  return std::make_unique<CowlDevToolsManagerDelegate>();
}

void CowlContentBrowserClient::set_browser_context(
    std::unique_ptr<CowlBrowserContext> ctx) {
  browser_context_ = std::move(ctx);
}

void CowlContentBrowserClient::RegisterBrowserInterfaceBindersForFrame(
    content::RenderFrameHost* render_frame_host,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  map->Add<cowl::mojom::OsService>(
      base::BindRepeating(
          [](content::RenderFrameHost* frame_host,
             mojo::PendingReceiver<cowl::mojom::OsService> receiver) {
            // Self-owned: destructs when pipe closes
            // (frame destroyed/navigated).
            new OsServiceImpl(frame_host, std::move(receiver));
          }));
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
CowlContentBrowserClient::CreateNonNetworkNavigationURLLoaderFactory(
    const std::string& scheme,
    content::FrameTreeNodeId frame_tree_node_id) {
  if (scheme == kOsScheme) {
    return CowlOsURLLoaderFactory::Create(surface_base_path_);
  }
  return {};
}

void CowlContentBrowserClient::RegisterNonNetworkSubresourceURLLoaderFactories(
    int render_process_id,
    int render_frame_id,
    const std::optional<url::Origin>& request_initiator_origin,
    NonNetworkURLLoaderFactoryMap* factories) {
  factories->emplace(kOsScheme,
                     CowlOsURLLoaderFactory::Create(surface_base_path_));
}

void CowlContentBrowserClient::
    RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
        content::BrowserContext* browser_context,
        const std::optional<url::Origin>& request_initiator,
        network::mojom::RequestDestination request_destination,
        NonNetworkURLLoaderFactoryMap* factories) {
  // |request_initiator| / |request_destination| (added in Chromium 149) are
  // filters cowl does not need; always register the os:// factory.
  factories->emplace(kOsScheme,
                     CowlOsURLLoaderFactory::Create(surface_base_path_));
}

void CowlContentBrowserClient::
    RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
        content::BrowserContext* browser_context,
        NonNetworkURLLoaderFactoryMap* factories) {
  factories->emplace(kOsScheme,
                     CowlOsURLLoaderFactory::Create(surface_base_path_));
}

}  // namespace cowl
