// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/app/shell_bootstrap.h"

#include <string>
#include <utility>

#include "base/allocator/partition_alloc_support.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/message_loop/message_pump_type.h"
#include "base/path_service.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread.h"
#include "components/discardable_memory/service/discardable_shared_memory_manager.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/aura/env.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/display/screen.h"
#include "ui/gfx/font_util.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/wm_state.h"

namespace views_shell {

// The process-wide accessibility mode: native APIs only, never web content.
class ShellAXPlatformDelegate : public ui::AXPlatform::Delegate {
 public:
  ShellAXPlatformDelegate() = default;
  ShellAXPlatformDelegate(const ShellAXPlatformDelegate&) = delete;
  ShellAXPlatformDelegate& operator=(const ShellAXPlatformDelegate&) = delete;
  ~ShellAXPlatformDelegate() override = default;

  ui::AXMode GetAccessibilityMode() override { return mode_; }
  void OnMinimalPropertiesUsed() override { Enable(ui::AXMode::kNativeAPIs); }
  void OnPropertiesUsedInBrowserUI() override {
    Enable(ui::AXMode::kNativeAPIs);
  }

 private:
  void Enable(uint32_t flag) {
    if (mode_.has_mode(flag)) {
      return;
    }
    mode_ |= ui::AXMode(flag);
    ax_platform_.NotifyModeAdded(ui::AXMode(flag));
  }

  ui::AXMode mode_;
  ui::AXPlatform ax_platform_{*this};
};

// Every top-level Widget is a desktop widget: one Wayland surface each. Owns
// the LayoutProvider that Label and the other controls read their metrics from.
class ShellViewsDelegate : public views::ViewsDelegate {
 public:
  ShellViewsDelegate() = default;
  ShellViewsDelegate(const ShellViewsDelegate&) = delete;
  ShellViewsDelegate& operator=(const ShellViewsDelegate&) = delete;
  ~ShellViewsDelegate() override = default;

  void OnBeforeWidgetInit(
      views::Widget::InitParams* params,
      views::internal::NativeWidgetDelegate* delegate) override {
    if (params->opacity ==
        views::Widget::InitParams::WindowOpacity::kInferred) {
      params->opacity = views::Widget::InitParams::WindowOpacity::kOpaque;
    }
    if (!params->native_widget) {
      params->native_widget = new views::DesktopNativeWidgetAura(delegate);
    }
  }

 private:
  views::LayoutProvider layout_provider_;
};

ShellBootstrap::ShellBootstrap(const ShellBootstrapParams& params)
    : params_(params) {}

ShellBootstrap::~ShellBootstrap() {
  // 11, 10, 9, 8: the Views and Aura layer, top down.
  wm_state_.reset();
  views_delegate_.reset();
  desktop_screen_.reset();
  if (input_method_initialized_) {
    ui::ShutdownInputMethod();
  }
  env_.reset();
  if (main_task_executor_) {
    ui::Clipboard::DestroyClipboardForCurrentThread();
  }
  // 7: the in-process viz host; every compositor has gone with aura::Env.
  context_factory_.reset();
  // 6, 5.
  if (resource_bundle_initialized_) {
    ui::ResourceBundle::CleanupSharedInstance();
  }
  if (discardable_memory_manager_) {
    base::DiscardableMemoryAllocator::SetInstance(nullptr);
    discardable_memory_manager_.reset();
  }
  // 3: mojo.
  ipc_support_.reset();
  if (mojo_io_thread_) {
    mojo_io_thread_->Stop();
    mojo_io_thread_.reset();
  }
  // 1: base. The accessibility delegate was created right after it.
  ax_platform_delegate_.reset();
  memory_consumer_registry_.reset();
  if (thread_pool_started_) {
    base::ThreadPoolInstance::Get()->Shutdown();
  }
  main_task_executor_.reset();
}

bool ShellBootstrap::Init() {
  // 1. base, and the memory consumer registry. The accessibility platform comes right after it: Views and Aura
  // ask for it from their first objects on.
  // PartitionAlloc is configured the way content configures a browser process
  // (process type ""): the viz compositor thread reconfigures its
  // scheduler-loop quarantine branch on start, which needs the thread cache
  // these calls enable.
  auto* partition_alloc_support = base::allocator::PartitionAllocSupport::Get();
  partition_alloc_support->ReconfigureEarlyish("");
  base::FeatureList::InitInstance(std::string(), std::string());
  partition_alloc_support->ReconfigureAfterFeatureListInit("");
  main_task_executor_ =
      std::make_unique<base::SingleThreadTaskExecutor>(base::MessagePumpType::UI);
  partition_alloc_support->ReconfigureAfterTaskRunnerInit("");
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("views_shell");
  thread_pool_started_ = true;
  // Memory consumers (the discardable manager below, cc's caches) register
  // with the process's registry; outside //content nothing coordinates them,
  // so the registry is base's placeholder for standalone programs.
  memory_consumer_registry_ = std::make_unique<
      base::ScopedMemoryConsumerRegistry<base::DummyMemoryConsumerRegistry>>();
  ax_platform_delegate_ = std::make_unique<ShellAXPlatformDelegate>();

  // 2. Ozone, single process: the UI side (Wayland connection, with threaded
  // event polling because the GPU thread may watch the same display) and the
  // GPU side (surface factory, buffer manager) live in this one process.
  // aura::Env's own InitializeForUI below is then a no-op.
  ui::OzonePlatform::InitParams ozone_params;
  ozone_params.single_process = true;
  if (!ui::OzonePlatform::InitializeForUI(ozone_params)) {
    LOG(ERROR) << "Ozone failed to initialise its UI side";
    return false;
  }
  ui::OzonePlatform::InitializeForGPU(ozone_params);

  // 3. mojo, with its IO thread (demo_main.cc's InitMojo). The GPU channel's
  // client end listens on the same thread.
  mojo::core::Init();
  mojo_io_thread_ = std::make_unique<base::Thread>("ViewsShellMojoIO");
  if (!mojo_io_thread_->StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "the mojo IO thread failed to start";
    return false;
  }
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_io_thread_->task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // 4. ICU and fonts.
  if (!base::i18n::InitializeICU()) {
    LOG(ERROR) << "ICU failed to initialise";
    return false;
  }
  gfx::InitializeFonts();

  // 5. Discardable memory: the in-process manager (as in a browser process)
  // serves Skia's and cc's discardable allocations.
  discardable_memory_manager_ =
      std::make_unique<discardable_memory::DiscardableSharedMemoryManager>();
  base::DiscardableMemoryAllocator::SetInstance(
      discardable_memory_manager_.get());

  // 6. Resources. Debt D2 (docs/architecture.md): ui_test_pak until
  // views-shell repacks its own views_shell.pak.
  ui::RegisterPathProvider();
  base::FilePath pak_path;
  if (!base::PathService::Get(ui::UI_TEST_PAK, &pak_path)) {
    LOG(ERROR) << "no resource pak path";
    return false;
  }
  ui::ResourceBundle::InitSharedInstanceWithPakPath(pak_path);
  resource_bundle_initialized_ = true;

  // 7. The in-process viz host. It must exist before any ui::Compositor.
  context_factory_ =
      std::make_unique<ViewsShellContextFactory>(params_.compositing_mode);
  if (!context_factory_->Initialize(mojo_io_thread_->task_runner())) {
    return false;
  }

  // 8. aura::Env, compositing through that factory.
  env_ = aura::Env::CreateInstance();
  env_->set_context_factory(context_factory_.get());

  // 9. The input method (the platform's: on Wayland, text-input-v3).
  ui::InitializeInputMethod();
  input_method_initialized_ = true;

  // 10. The desktop screen (wl_output, through Ozone).
  desktop_screen_ = views::CreateDesktopScreen();

  // 11. The Views delegate and the window-manager state.
  views_delegate_ = std::make_unique<ShellViewsDelegate>();
  wm_state_ = std::make_unique<wm::WMState>();
  return true;
}

}  // namespace views_shell
