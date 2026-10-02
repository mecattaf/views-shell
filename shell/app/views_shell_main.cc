// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// views-shell: a content-free Views program on Ozone/Wayland. No //content, no
// Blink, no V8. The process bootstrap (single-process Ozone, mojo, the
// in-process viz host, aura::Env, the ViewsDelegate, wm::WMState) is
// app/shell_bootstrap.h; this file keeps the flags and the widgets.
//
// --software-compositing (the default) or --gpu-compositing picks how the
// in-process viz draws (app/views_shell_context_factory.h).
//
// views-shell draws only on layer surfaces (rule R1): every top-level widget
// is created on a registered SurfaceSpec (app/surface_spec.h), and the
// ViewsDelegate CHECKs it. There is no window mode: without --bar or
// --left-tabs the program says so and exits 2.
//
// --bar puts the bar (bar/bar_view.h: a FlexLayout row with the clock on the
// right) on the "bar" surface: top layer, anchored to the top edge, 32 px
// high, exclusive zone, namespace "views-shell-bar". With --bar --demo-popup a
// views::MenuRunner menu opens from the bar one second after the bar's first
// paint; on Wayland the menu becomes an xdg_popup parented onto the layer
// surface via zwlr_layer_surface_v1.get_popup.
//
// --left-tabs puts the workspace strip (Chrome's tab, ported under tabs/; one
// tab per workspace) and a content area on the "left-tabs" surface, a panel on
// the left edge. --workspace-source=static|niri picks the workspaces; without
// it, niri when NIRI_SOCKET is set, else a static list.
//
// --theme <dir> (or --theme=<dir>) wears an Omarchy theme directory (rule R17, style/): it is
// read and resolved before anything else and applied before the first widget.
// Without --theme the colours are stock ui/color.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "views_shell/app/shell_bootstrap.h"
#include "views_shell/app/surface_spec.h"
#include "views_shell/app/views_shell_context_factory.h"
#include "views_shell/bar/bar_view.h"
#include "views_shell/style/theme_mixer.h"
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
constexpr char kSoftwareCompositing[] = "software-compositing";
constexpr char kGpuCompositing[] = "gpu-compositing";
constexpr char kTheme[] = "theme";

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

// The delegate of the one top-level widget: the bar (BarView), or with
// --left-tabs the workspace strip. Closing it ends the program. With
// --demo-popup it also owns the MenuRunner that opens from the bar one second
// after the bar's first painted frame.
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
    auto bar = std::make_unique<BarView>(base::DefaultClock::GetInstance());
    if (demo_popup) {
      // The bar reports its first paint; the menu then opens one second
      // later (SPEC.md C4.3).
      bar->SetFirstPaintCallback(base::BindOnce(
          &ShellWindowDelegate::ScheduleDemoPopup, base::Unretained(this)));
    }
    SetContentsView(std::move(bar));
  }
  ~ShellWindowDelegate() override = default;

  // --left-tabs: the workspace strip on the left, a content area in the
  // theme's ground on the right. The strip fills when the source answers.
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
    content->SetBackground(views::CreateSolidBackground(ui::kColorSysBase));
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
    const int bar_height = FindSurfaceSpec("bar")->height;
    menu_runner_->RunMenuAt(widget,
                            /*button_controller=*/nullptr,
                            gfx::Rect(16, 0, 120, bar_height),
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

// Creates the top-level widget for `spec` (the only way views-shell makes one;
// rule R1) and shows it.
std::unique_ptr<views::Widget> CreateSurfaceWidget(
    const SurfaceSpec& spec,
    views::WidgetDelegate* delegate) {
  auto widget = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  ApplySurfaceSpec(spec, &params);
  params.delegate = delegate;
  params.name = kAppId;
  params.wayland_app_id = kAppId;
  params.wm_class_name = kAppId;
  params.wm_class_class = kAppId;
  widget->Init(std::move(params));
  widget->Show();
  return widget;
}

int ShellMain() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  int run_for_seconds = 0;
  if (command_line->HasSwitch(kRunForSeconds) &&
      !base::StringToInt(command_line->GetSwitchValueASCII(kRunForSeconds),
                         &run_for_seconds)) {
    LOG(ERROR) << "--" << kRunForSeconds << " wants an integer";
    return 2;
  }
  if (command_line->HasSwitch(kSoftwareCompositing) &&
      command_line->HasSwitch(kGpuCompositing)) {
    LOG(ERROR) << "--" << kSoftwareCompositing << " and --" << kGpuCompositing
               << " exclude each other";
    return 2;
  }
  ShellBootstrapParams bootstrap_params;
  bootstrap_params.compositing_mode = command_line->HasSwitch(kGpuCompositing)
                                          ? CompositingMode::kGpu
                                          : CompositingMode::kSoftware;
  const bool bar = command_line->HasSwitch(kBar);
  // The demo popup is a menu on the bar; without --bar there is no bar to
  // open it from (SPEC.md C4.3 runs both).
  const bool demo_popup = bar && command_line->HasSwitch(kDemoPopup);
  // The workspace strip is its own surface, not the bar's contents.
  const bool left_tabs = !bar && command_line->HasSwitch(kLeftTabs);
  if (!bar && !left_tabs) {
    // Rule R1: no xdg_toplevel, so no window mode. Refused before anything
    // connects to the compositor. (Chapter 1's C3.4 window run is superseded.)
    LOG(ERROR) << "views-shell draws only on layer surfaces (rule R1); pass "
                  "--bar (or --left-tabs)";
    return 2;
  }
  const SurfaceSpec* surface = FindSurfaceSpec(bar ? "bar" : "left-tabs");
  CHECK(surface);

  // The theme is read and resolved before the process comes up, so a bad
  // theme directory costs no compositor connection.
  std::optional<ResolvedTheme> theme;
  base::FilePath theme_dir;
  if (command_line->HasSwitch(kTheme)) {
    // --theme=<dir>, or --theme <dir> (base::CommandLine leaves a value after
    // a space as the first positional argument).
    theme_dir = command_line->GetSwitchValuePath(kTheme);
    if (theme_dir.empty() && !command_line->GetArgs().empty()) {
      theme_dir = base::FilePath(command_line->GetArgs().front());
    }
    if (theme_dir.empty()) {
      LOG(ERROR) << "--" << kTheme << " wants a theme directory";
      return 2;
    }
    base::expected<ResolvedTheme, std::string> resolved =
        LoadAndResolveTheme(theme_dir);
    if (!resolved.has_value()) {
      LOG(ERROR) << "--" << kTheme << ": " << resolved.error();
      return 2;
    }
    theme = std::move(resolved).value();
  }
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

  ShellBootstrap bootstrap(bootstrap_params);
  if (!bootstrap.Init()) {
    return 1;
  }

  // Chrome's tab colours, in terms of the stock kColorSys* ids (tabs/).
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddTabColorMixer));

  // The theme: the seed on the native theme and the pin mixer appended last
  // (style/theme_mixer.h), before the first widget.
  ThemeController theme_controller;
  if (theme) {
    LOG(INFO) << "theme: " << theme_dir.value()
              << " mode "
              << (theme->color_mode == ui::ColorProviderKey::ColorMode::kDark
                      ? "dark"
                      : "light")
              << " seed " << ToHexColor(theme->seed) << ", "
              << theme->pins.size() << " kColorSys pins";
    theme_controller.Apply(std::move(*theme),
                           ui::NativeTheme::GetInstanceForNativeUi());
  }

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  ShellWindowDelegate window_delegate(run_loop.QuitClosure(), demo_popup,
                                      std::move(workspace_source));
  std::unique_ptr<views::Widget> widget =
      CreateSurfaceWidget(*surface, &window_delegate);

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
  // The bootstrap tears the process down in reverse when it goes out of scope.
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
