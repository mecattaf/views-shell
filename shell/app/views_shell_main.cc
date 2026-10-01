// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// views-shell: a content-free Views program on Ozone/Wayland. Shaped like
// ui/views/examples/examples_main_proc.cc: single-process Ozone, aura::Env,
// wm::WMState, a ViewsDelegate and one Widget. No //content, no Blink, no V8.

#include <memory>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/message_loop/message_pump_type.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/allow_check_is_test_for_testing.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/time/time.h"
#include "mojo/core/embedder/embedder.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/aura/env.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/compositor/test/in_process_context_factory.h"
#include "ui/compositor/test/test_context_factories.h"
#include "ui/display/screen.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/platform_window/common/platform_window_defaults.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/wm_state.h"

namespace views_shell {
namespace {

constexpr char kAppId[] = "views-shell";
constexpr char kRunForSeconds[] = "run-for-seconds";

// The process-wide accessibility mode: native APIs only, never web content.
class ShellAXPlatformDelegate : public ui::AXPlatform::Delegate {
 public:
  ShellAXPlatformDelegate() = default;
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

// The one window: a black ground with the label, as in the All Black theme
// (style/tokens/README.md). Closing it ends the program.
class ShellWindowDelegate : public views::WidgetDelegate {
 public:
  explicit ShellWindowDelegate(base::OnceClosure on_close)
      : on_close_(std::move(on_close)) {
    SetTitle(u"views-shell");
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::FillLayout>());
    contents->SetBackground(views::CreateSolidBackground(SK_ColorBLACK));
    auto* label =
        contents->AddChildView(std::make_unique<views::Label>(u"views-shell"));
    label->SetEnabledColor(SK_ColorWHITE);
    label->SetBackgroundColor(SK_ColorBLACK);
    SetContentsView(std::move(contents));
  }
  ~ShellWindowDelegate() override = default;

  // views::WidgetDelegate:
  void WindowClosing() override {
    if (on_close_) {
      std::move(on_close_).Run();
    }
  }

 private:
  base::OnceClosure on_close_;
};

std::unique_ptr<views::Widget> CreateShellWidget(
    views::WidgetDelegate* delegate) {
  auto widget = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.name = kAppId;
  params.wayland_app_id = kAppId;
  params.wm_class_name = kAppId;
  params.wm_class_class = kAppId;
  params.bounds = gfx::Rect(0, 0, 640, 120);
  widget->Init(std::move(params));
  widget->Show();
  return widget;
}

int ShellMain() {
  base::test::AllowCheckIsTestForTesting();  // Debt D1: the test context factory.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  int run_for_seconds = 0;
  if (command_line->HasSwitch(kRunForSeconds) &&
      !base::StringToInt(command_line->GetSwitchValueASCII(kRunForSeconds),
                         &run_for_seconds)) {
    LOG(ERROR) << "--" << kRunForSeconds << " wants an integer";
    return 2;
  }

  base::FeatureList::InitInstance(std::string(), std::string());

  base::SingleThreadTaskExecutor main_task_executor(base::MessagePumpType::UI);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("views_shell");

  ShellAXPlatformDelegate ax_platform_delegate;

  // Finding F2 (SPEC.md), debt D1: the test frame sink mints its own
  // LocalSurfaceIds, so WaylandWindow would never see the sequence it waits for
  // before ack_configure. Latch applied states immediately, as
  // aura::test::AuraTestHelper does. Must run before aura::Env opens the
  // Wayland connection.
  ui::test::EnableTestConfigForPlatformWindows();

  ui::OzonePlatform::InitParams ozone_params;
  ozone_params.single_process = true;
  ui::OzonePlatform::InitializeForGPU(ozone_params);

  mojo::core::Init();
  base::i18n::InitializeICU();
  gfx::InitializeFonts();
  gl::init::InitializeGLOneOff(gl::GpuPreference::kDefault);
  ui::RegisterPathProvider();

  base::TestDiscardableMemoryAllocator discardable_memory_allocator;
  base::DiscardableMemoryAllocator::SetInstance(&discardable_memory_allocator);

  base::FilePath ui_test_pak_path;
  CHECK(base::PathService::Get(ui::UI_TEST_PAK, &ui_test_pak_path));
  ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak_path);

  // The ContextFactory must exist before any Compositor is created.
  auto context_factories = std::make_unique<ui::TestContextFactories>(
      /*enable_pixel_output=*/false, /*output_to_window=*/true);

  std::unique_ptr<aura::Env> env = aura::Env::CreateInstance();
  env->set_context_factory(context_factories->GetContextFactory());
  ui::InitializeInputMethodForTesting();

  {
    ShellViewsDelegate views_delegate;
    wm::WMState wm_state;
    std::unique_ptr<display::Screen> desktop_screen =
        views::CreateDesktopScreen();

    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    ShellWindowDelegate window_delegate(run_loop.QuitClosure());
    std::unique_ptr<views::Widget> widget = CreateShellWidget(&window_delegate);

    if (run_for_seconds > 0) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, run_loop.QuitClosure(), base::Seconds(run_for_seconds));
    }
    run_loop.Run();

    widget->CloseNow();
    widget.reset();
    ui::ResourceBundle::CleanupSharedInstance();
  }

  ui::ShutdownInputMethod();
  env.reset();
  context_factories.reset();
  ui::Clipboard::DestroyClipboardForCurrentThread();
  base::ThreadPoolInstance::Get()->Shutdown();
  return 0;
}

}  // namespace
}  // namespace views_shell

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::AtExitManager at_exit;
  base::debug::EnableInProcessStackDumping();
  return views_shell::ShellMain();
}
