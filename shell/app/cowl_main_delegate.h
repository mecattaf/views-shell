// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COWL_SRC_COWL_MAIN_DELEGATE_H_
#define COWL_SRC_COWL_MAIN_DELEGATE_H_

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "content/public/app/content_main_delegate.h"

namespace cowl {

class CowlContentBrowserClient;
class CowlContentClient;
class CowlContentRendererClient;

class CowlMainDelegate : public content::ContentMainDelegate {
 public:
  CowlMainDelegate();
  ~CowlMainDelegate() override;

  CowlMainDelegate(const CowlMainDelegate&) = delete;
  CowlMainDelegate& operator=(const CowlMainDelegate&) = delete;

  // content::ContentMainDelegate:
  std::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  std::variant<int, content::MainFunctionParams> RunProcess(
      const std::string& process_type,
      content::MainFunctionParams main_function_params) override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;

 private:
  // ContentClient owns scheme registrations -- must outlive everything else.
  std::unique_ptr<CowlContentClient> content_client_;
  std::unique_ptr<CowlContentBrowserClient> browser_client_;
  std::unique_ptr<CowlContentRendererClient> renderer_client_;
};

}  // namespace cowl

#endif  // COWL_SRC_COWL_MAIN_DELEGATE_H_
