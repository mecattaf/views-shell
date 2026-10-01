// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// views-shell: a content-free Views program on Ozone/Wayland. Shaped like
// ui/views/examples/examples_main_proc.cc: single-process Ozone, aura::Env,
// wm::WMState, a ViewsDelegate and one Widget. No //content, no Blink, no V8.
//
// With --bar the widget is created as a wlr-layer-shell surface (rule R1:
// Views only on layer-shell surfaces): top layer, anchored to the top edge,
// 32 px high, exclusive zone, namespace "views-shell-bar". With --bar
// --demo-popup a views::MenuRunner menu opens from the bar one second after
// the bar's first paint; on Wayland the menu becomes an xdg_popup
// parented onto the layer surface via zwlr_layer_surface_v1.get_popup.
//
// With --left-tabs the window's contents are the workspace strip on the left
// (Chrome's tab, ported under tabs/; one tab per workspace) and a black
// content area on the right. --workspace-source=static|niri picks the
// workspaces; without it, niri when NIRI_SOCKET is set, else a static list.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_type.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
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
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider_manager.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/compositor/test/in_process_context_factory.h"
#include "ui/compositor/test/test_context_factories.h"
#include "ui/display/screen.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/platform_window/common/platform_window_defaults.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/wm_state.h"
#include "views_shell/tabs/tab_color_mixer.h"
#include "views_shell/tabs/workspace_source.h"
#include "views_shell/tabs/workspace_strip.h"

namespace views_shell {
namespace {

constexpr char kAppId[] = "views-shell";
constexpr char kRunForSeconds[] = "run-for-seconds";
constexpr char kBar[] = "bar";
constexpr char kDemoPopup[] = "demo-popup";
constexpr char kLeftTabs[] = "left-tabs";
constexpr char kWorkspaceSource[] = "workspace-source";

// The bar (SPEC.md C4.2, open question Q2's default): top layer, anchored to
// the top edge, 32 px high, exclusive zone of the same height, namespace
// "views-shell-bar".
constexpr int kBarHeight = 32;
constexpr char kBarNamespace[] = "views-shell-bar";

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

// Contents view that reports its first paint, so --demo-popup can
// open the menu one second after the first frame (SPEC.md C4.3).
class FirstPaintView : public views::View {
 public:
  explicit FirstPaintView(base::OnceClosure on_first_paint)
      : on_first_paint_(std::move(on_first_paint)) {}
  FirstPaintView(const FirstPaintView&) = delete;
  FirstPaintView& operator=(const FirstPaintView&) = delete;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    if (on_first_paint_) {
      std::move(on_first_paint_).Run();
    }
  }

 private:
  base::OnceClosure on_first_paint_;
};

// Menu command target for the --demo-popup menu. The items do nothing: the
// demo proves the xdg_popup path, not command handling.
class DemoMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  DemoMenuDelegate() = default;
  ~DemoMenuDelegate() override = default;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {}
  bool IsCommandIdEnabled(int command_id) const override { return true; }
};

// The one window: a black ground with the label, as in the All Black theme
// (style/tokens/README.md). Closing it ends the program. With --demo-popup it
// also owns the MenuRunner that opens from the bar one second after the first
// painted frame.
class ShellWindowDelegate : public views::WidgetDelegate {
 public:
  // `workspace_source` is non-null only with --left-tabs.
  ShellWindowDelegate(base::OnceClosure on_close,
                      bool demo_popup,
                      std::unique_ptr<WorkspaceSource> workspace_source)
      : on_close_(std::move(on_close)),
        demo_popup_(demo_popup),
        workspace_source_(std::move(workspace_source)) {
    SetTitle(u"views-shell");
    if (workspace_source_) {
      SetContentsView(BuildLeftTabsContents());
      return;
    }
    base::OnceClosure on_first_paint;
    if (demo_popup) {
      // The contents view reports its first paint; the menu then
      // opens one second later (SPEC.md C4.3).
      on_first_paint = base::BindOnce(&ShellWindowDelegate::ScheduleDemoPopup,
                                      base::Unretained(this));
    }
    SetContentsView(BuildContents(std::move(on_first_paint)));
  }
  ~ShellWindowDelegate() override = default;

  // Builds the contents: black ground, white label.
  std::unique_ptr<views::View> BuildContents(
      base::OnceClosure on_first_paint) {
    auto contents = on_first_paint
                        ? std::make_unique<FirstPaintView>(
                              std::move(on_first_paint))
                        : std::make_unique<views::View>();
    contents->SetLayoutManager(std::make_unique<views::FillLayout>());
    contents->SetBackground(views::CreateSolidBackground(SK_ColorBLACK));
    auto* label =
        contents->AddChildView(std::make_unique<views::Label>(u"views-shell"));
    label->SetEnabledColor(SK_ColorWHITE);
    label->SetBackgroundColor(SK_ColorBLACK);
    return contents;
  }

  // --left-tabs: the workspace strip on the left, a black content area on the
  // right. The strip fills when the source answers.
  std::unique_ptr<views::View> BuildLeftTabsContents() {
    auto contents = std::make_unique<views::View>();
    auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    strip_ = contents->AddChildView(std::make_unique<WorkspaceStrip>(
        base::BindRepeating([](const Workspace& workspace) {
          LOG(INFO) << "left-tabs: selected workspace " << workspace.id << " ("
                    << base::UTF16ToUTF8(workspace.title) << ")";
        })));
    auto* content = contents->AddChildView(std::make_unique<views::View>());
    content->SetBackground(views::CreateSolidBackground(SK_ColorBLACK));
    layout->SetFlexForView(content, 1);
    workspace_source_->Fetch(base::BindOnce(
        &ShellWindowDelegate::OnWorkspaces, weak_ptr_factory_.GetWeakPtr()));
    return contents;
  }

  void OnWorkspaces(std::vector<Workspace> workspaces) {
    if (!strip_) {
      return;
    }
    std::string active = "none";
    for (const Workspace& workspace : workspaces) {
      if (workspace.active) {
        active = base::UTF16ToUTF8(workspace.title);
      }
    }
    LOG(INFO) << "left-tabs: " << workspaces.size() << " workspaces from "
              << workspace_source_->name() << ", active " << active;
    strip_->SetWorkspaces(std::move(workspaces));
  }

  // First painted frame: open the menu one second from now.
  void ScheduleDemoPopup() {
    if (popup_scheduled_) {
      return;
    }
    popup_scheduled_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ShellWindowDelegate::OpenDemoPopup,
                       base::Unretained(this)),
        base::Seconds(1));
  }

  // Opens the demo menu from the bar. Runs a nested (nestable) message loop
  // until the menu closes; headless runs kill the process instead.
  void OpenDemoPopup() {
    if (!demo_popup_ || popup_opened_) {
      return;
    }
    views::Widget* widget = GetWidget();
    if (!widget) {
      return;
    }
    popup_opened_ = true;
    if (!menu_model_) {
      menu_model_ = std::make_unique<ui::SimpleMenuModel>(&menu_delegate_);
      menu_model_->AddItem(0, u"views-shell demo item");
      menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_model_->AddItem(1, u"second demo item");
    }
    if (!menu_runner_) {
      menu_runner_ = std::make_unique<views::MenuRunner>(
          menu_model_.get(), views::MenuRunner::NO_FLAGS);
    }
    menu_runner_->RunMenuAt(widget,
                            /*button_controller=*/nullptr,
                            gfx::Rect(16, 0, 120, kBarHeight),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kMouse);
  }

  // Ends the menu's nested loop, if it is running (shutdown ordering).
  void CancelDemoPopup() {
    if (menu_runner_ && menu_runner_->IsRunning()) {
      menu_runner_->Cancel();
    }
  }

  // views::WidgetDelegate:
  void WindowClosing() override {
    strip_ = nullptr;
    CancelDemoPopup();
    if (on_close_) {
      std::move(on_close_).Run();
    }
  }

 private:
  base::OnceClosure on_close_;
  bool demo_popup_;
  bool popup_scheduled_ = false;
  bool popup_opened_ = false;
  DemoMenuDelegate menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<WorkspaceSource> workspace_source_;
  raw_ptr<WorkspaceStrip> strip_ = nullptr;
  base::WeakPtrFactory<ShellWindowDelegate> weak_ptr_factory_{this};
};

std::unique_ptr<views::Widget> CreateShellWidget(ShellWindowDelegate* delegate,
                                                 bool bar,
                                                 bool left_tabs) {
  auto widget = std::make_unique<views::Widget>();
  // The bar is frameless: a standard frame would add its caption to the
  // requested height.
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      bar ? views::Widget::InitParams::TYPE_WINDOW_FRAMELESS
          : views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.name = kAppId;
  params.wayland_app_id = kAppId;
  params.wm_class_name = kAppId;
  params.wm_class_class = kAppId;
  if (bar) {
    // SPEC.md C4.2: the bar is a layer surface on the top layer, anchored to
    // the top edge (stretched left+right, so the compositor picks the width),
    // 32 px high with an exclusive zone, namespace "views-shell-bar". The
    // plumbing is shell/patches/views-shell-ozone-layer-shell.patch:
    // Widget::InitParams::layer_shell -> PlatformWindowInitProperties ->
    // PlatformWindowType::kLayerShell -> WaylandLayerShellWindow.
    ui::LayerShellProperties layer_shell;
    layer_shell.layer = ui::LayerShellLayer::kTop;
    layer_shell.anchor =
        ui::kLayerShellAnchorTop | ui::kLayerShellAnchorLeft |
        ui::kLayerShellAnchorRight;
    layer_shell.exclusive_zone = kBarHeight;
    layer_shell.keyboard_interactivity =
        ui::LayerShellKeyboardInteractivity::kNone;
    layer_shell.layer_namespace = kBarNamespace;
    params.layer_shell = layer_shell;
    params.bounds = gfx::Rect(0, 0, 1920, kBarHeight);
  } else if (left_tabs) {
    params.bounds = gfx::Rect(0, 0, 960, 540);
  } else {
    params.bounds = gfx::Rect(0, 0, 640, 120);
  }
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
  const bool bar = command_line->HasSwitch(kBar);
  // The demo popup is a menu on the bar; without --bar there is no bar to
  // open it from (SPEC.md C4.3 runs both).
  const bool demo_popup = bar && command_line->HasSwitch(kDemoPopup);
  // The workspace strip is a window's contents, not the bar's.
  const bool left_tabs = !bar && command_line->HasSwitch(kLeftTabs);
  std::unique_ptr<WorkspaceSource> workspace_source;
  if (left_tabs) {
    const std::string source_name =
        command_line->GetSwitchValueASCII(kWorkspaceSource);
    workspace_source = CreateWorkspaceSource(source_name);
    if (!workspace_source) {
      LOG(ERROR) << "--" << kWorkspaceSource << " wants static or niri, not "
                 << source_name;
      return 2;
    }
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

  // Chrome's tab colours, in terms of the stock kColorSys* ids (tabs/).
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddTabColorMixer));

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
    ShellWindowDelegate window_delegate(run_loop.QuitClosure(), demo_popup,
                                        std::move(workspace_source));
    std::unique_ptr<views::Widget> widget =
        CreateShellWidget(&window_delegate, bar, left_tabs);

    if (run_for_seconds > 0) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](ShellWindowDelegate* delegate, base::OnceClosure quit) {
                delegate->CancelDemoPopup();
                std::move(quit).Run();
              },
              &window_delegate, run_loop.QuitClosure()),
          base::Seconds(run_for_seconds));
    }
    run_loop.Run();

    window_delegate.CancelDemoPopup();
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
