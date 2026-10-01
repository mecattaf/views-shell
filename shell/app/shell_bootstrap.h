// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The process bootstrap of views-shell (docs/architecture.md §3): everything a
// content-free Views program needs before its first Widget, in order, and its
// teardown in reverse. views_shell_main.cc keeps only its flags and widgets;
// later chapters add content without re-reading this.
//
// Order of Init(), each step owned here and torn down in reverse:
//   1. base: the feature list, the UI SingleThreadTaskExecutor, the thread
//      pool, then the accessibility platform delegate (native APIs only);
//   2. Ozone in single-process mode: InitializeForUI (the Wayland connection),
//      then InitializeForGPU (the GPU-side surface factory);
//   3. mojo: mojo::core::Init() and ScopedIPCSupport on a mojo IO thread;
//   4. ICU and fonts;
//   5. the discardable memory allocator
//      (discardable_memory::DiscardableSharedMemoryManager);
//   6. the ResourceBundle (debt D2: still ui_test_pak);
//   7. the context factory (app/views_shell_context_factory.h: the in-process
//      viz host);
//   8. aura::Env with that context factory;
//   9. the input method (ui::InitializeInputMethod());
//  10. the desktop screen;
//  11. the ShellViewsDelegate (its LayoutProvider, every top-level Widget a
//      DesktopNativeWidgetAura) and wm::WMState.

#ifndef VIEWS_SHELL_APP_SHELL_BOOTSTRAP_H_
#define VIEWS_SHELL_APP_SHELL_BOOTSTRAP_H_

#include <memory>

#include "views_shell/app/views_shell_context_factory.h"

namespace aura {
class Env;
}

namespace base {
class SingleThreadTaskExecutor;
class Thread;
}  // namespace base

namespace discardable_memory {
class DiscardableSharedMemoryManager;
}

namespace display {
class Screen;
}

namespace mojo::core {
class ScopedIPCSupport;
}

namespace wm {
class WMState;
}

namespace views_shell {

class ShellAXPlatformDelegate;
class ShellViewsDelegate;

struct ShellBootstrapParams {
  CompositingMode compositing_mode = CompositingMode::kSoftware;
};

class ShellBootstrap {
 public:
  explicit ShellBootstrap(const ShellBootstrapParams& params);
  ShellBootstrap(const ShellBootstrap&) = delete;
  ShellBootstrap& operator=(const ShellBootstrap&) = delete;
  // Tears down whatever Init() brought up, in reverse order.
  ~ShellBootstrap();

  // Brings the process up to the point where Widgets can be created. Returns
  // false, having logged why, if a step failed; the destructor still tears
  // down the steps that ran.
  bool Init();

  ViewsShellContextFactory* context_factory() {
    return context_factory_.get();
  }

 private:
  const ShellBootstrapParams params_;
  bool resource_bundle_initialized_ = false;
  bool input_method_initialized_ = false;

  // Declaration order is Init() order; members are destroyed in reverse.
  std::unique_ptr<base::SingleThreadTaskExecutor> main_task_executor_;
  bool thread_pool_started_ = false;
  std::unique_ptr<base::Thread> mojo_io_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  std::unique_ptr<discardable_memory::DiscardableSharedMemoryManager>
      discardable_memory_manager_;
  std::unique_ptr<ViewsShellContextFactory> context_factory_;
  std::unique_ptr<aura::Env> env_;
  std::unique_ptr<display::Screen> desktop_screen_;
  std::unique_ptr<ShellAXPlatformDelegate> ax_platform_delegate_;
  std::unique_ptr<ShellViewsDelegate> views_delegate_;
  std::unique_ptr<wm::WMState> wm_state_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_APP_SHELL_BOOTSTRAP_H_
