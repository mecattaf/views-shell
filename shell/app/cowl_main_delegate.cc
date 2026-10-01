// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cowl/src/cowl_main_delegate.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/i18n/icu_util.h"
#include "base/path_service.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "ui/base/resource/resource_bundle.h"

#include "cowl/src/cowl_content_browser_client.h"
#include "cowl/src/cowl_content_client.h"
#include "cowl/src/cowl_content_renderer_client.h"

namespace cowl {

namespace {

constexpr char kPakPathSwitch[] = "pak-path";
constexpr char kDefaultPakPath[] = "/usr/share/cowl";

// Find the directory containing .pak resource files.
// Priority: --pak-path= flag > exe directory > /usr/share/cowl/
base::FilePath GetPakPath() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(kPakPathSwitch)) {
    base::FilePath path(cmd->GetSwitchValueASCII(kPakPathSwitch));
    if (base::DirectoryExists(path))
      return path;
  }

  // Check executable directory (component build places .pak files here).
  base::FilePath exe_dir;
  if (base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    if (base::PathExists(exe_dir.Append("content_resources.pak")))
      return exe_dir;
  }

  // System installation path.
  base::FilePath system_path(kDefaultPakPath);
  if (base::DirectoryExists(system_path))
    return system_path;

  // Fallback to exe directory even if .pak files aren't found yet.
  return exe_dir;
}

}  // namespace

CowlMainDelegate::CowlMainDelegate() = default;
CowlMainDelegate::~CowlMainDelegate() = default;

std::optional<int> CowlMainDelegate::BasicStartupComplete() {
  // Register the ContentClient BEFORE anything else.
  // ContentClient::AddAdditionalSchemes() is called by the Content layer
  // during initialization, registering "os" as a standard scheme.
  // This MUST happen before any GURL("os://...") is constructed.
  content_client_ = std::make_unique<CowlContentClient>();
  content::SetContentClient(content_client_.get());

  return std::nullopt;
}

void CowlMainDelegate::PreSandboxStartup() {
  // CRITICAL: This runs in ALL process types (browser, renderer, GPU, utility)
  // BEFORE the sandbox locks down filesystem access. ResourceBundle and ICU
  // must be initialized here so that renderer and GPU processes can function.

  // NOTE: Do NOT call base::i18n::InitializeICU() here.
  // The Content API initializes ICU in ContentMainRunnerImpl::Initialize()
  // before calling PreSandboxStartup(). Double-init triggers a DCHECK.

  // Initialize the ResourceBundle with .pak files.
  // The renderer process needs this to load Chromium internal resources
  // (error pages, DevTools frontend, Blink resources, etc.)
  base::FilePath pak_path = GetPakPath();

  ui::ResourceBundle::InitSharedInstanceWithPakPath(
      pak_path.Append("content_resources.pak"));

  // Load additional resource paks if they exist.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

  base::FilePath ui_resources = pak_path.Append("ui_resources_100_percent.pak");
  if (base::PathExists(ui_resources))
    rb.AddDataPackFromPath(ui_resources, ui::k100Percent);

  base::FilePath ui_strings = pak_path.Append("ui_strings.pak");
  if (base::PathExists(ui_strings))
    rb.AddDataPackFromPath(ui_strings, ui::kScaleFactorNone);

  base::FilePath content_shell_pak =
      pak_path.Append("content_shell_resources.pak");
  if (base::PathExists(content_shell_pak))
    rb.AddDataPackFromPath(content_shell_pak, ui::kScaleFactorNone);

  // Localized UI strings (IDS_APP_COPY, etc.). Native Views controls -- e.g.
  // views::Label / views::Textfield -- build context-menu models with these
  // string ids during construction; without a locale pak loaded, the very
  // first Views control fatally fails the ResourceBundle string lookup. cowl
  // never needed these for the *web* canvas, but the Views shell does. The
  // localized-string lookup falls back to the data packs, so adding the en-US
  // locale pack here makes those ids resolvable.
  base::FilePath locale_pak =
      pak_path.Append("locales").Append("en-US.pak");
  if (base::PathExists(locale_pak))
    rb.AddDataPackFromPath(locale_pak, ui::kScaleFactorNone);
}

std::variant<int, content::MainFunctionParams>
CowlMainDelegate::RunProcess(
    const std::string& process_type,
    content::MainFunctionParams main_function_params) {
  // Fall through to Content API's default browser process main.
  return std::move(main_function_params);
}

content::ContentBrowserClient*
CowlMainDelegate::CreateContentBrowserClient() {
  browser_client_ = std::make_unique<CowlContentBrowserClient>();
  return browser_client_.get();
}

content::ContentRendererClient*
CowlMainDelegate::CreateContentRendererClient() {
  renderer_client_ = std::make_unique<CowlContentRendererClient>();
  return renderer_client_.get();
}

}  // namespace cowl
