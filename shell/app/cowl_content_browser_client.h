// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COWL_SRC_COWL_CONTENT_BROWSER_CLIENT_H_
#define COWL_SRC_COWL_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "content/public/browser/content_browser_client.h"
#include "mojo/public/cpp/bindings/binder_map.h"

namespace cowl {

class CowlBrowserContext;
class CowlBrowserMainParts;

class CowlContentBrowserClient : public content::ContentBrowserClient {
 public:
  CowlContentBrowserClient();
  ~CowlContentBrowserClient() override;

  CowlContentBrowserClient(const CowlContentBrowserClient&) = delete;
  CowlContentBrowserClient& operator=(const CowlContentBrowserClient&) = delete;

  // content::ContentBrowserClient overrides:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  std::string GetUserAgent() override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;

  // os:// scheme: navigation URL loader factory.
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  CreateNonNetworkNavigationURLLoaderFactory(
      const std::string& scheme,
      content::FrameTreeNodeId frame_tree_node_id) override;

  // os:// scheme: subresource URL loader factory.
  void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id,
      int render_frame_id,
      const std::optional<url::Origin>& request_initiator_origin,
      NonNetworkURLLoaderFactoryMap* factories) override;

  // os:// scheme: worker main resource URL loader factory.
  // Chromium 149 added |request_initiator| + |request_destination| params.
  void RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
      content::BrowserContext* browser_context,
      const std::optional<url::Origin>& request_initiator,
      network::mojom::RequestDestination request_destination,
      NonNetworkURLLoaderFactoryMap* factories) override;

  // os:// scheme: service worker update URL loader factory.
  void RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
      content::BrowserContext* browser_context,
      NonNetworkURLLoaderFactoryMap* factories) override;

  CowlBrowserContext* browser_context() const { return browser_context_.get(); }
  void set_browser_context(std::unique_ptr<CowlBrowserContext> ctx);

  CowlBrowserMainParts* browser_main_parts() const {
    return browser_main_parts_;
  }
  void set_browser_main_parts(CowlBrowserMainParts* parts) {
    browser_main_parts_ = parts;
  }

  const base::FilePath& surface_base_path() const {
    return surface_base_path_;
  }
  void set_surface_base_path(const base::FilePath& path) {
    surface_base_path_ = path;
  }

 private:
  std::unique_ptr<CowlBrowserContext> browser_context_;
  raw_ptr<CowlBrowserMainParts> browser_main_parts_ = nullptr;
  base::FilePath surface_base_path_;
};

}  // namespace cowl

#endif  // COWL_SRC_COWL_CONTENT_BROWSER_CLIENT_H_
