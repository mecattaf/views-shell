// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/app/shell_content.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "base/types/expected.h"
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
#include "views_shell/app/keyboard_probe_view.h"
#include "views_shell/app/surface_spec.h"
#include "views_shell/bar/bar_view.h"
#include "views_shell/bar/workspace_strip.h"
#include "views_shell/notifications/notification_service.h"
#include "views_shell/tabs/tab_color_mixer.h"
#include "views_shell/tabs/workspace_source.h"
#include "views_shell/tabs/workspace_strip.h"
#include "views_shell/wm/adapters/scroll/scroll_adapter.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell {
namespace {

constexpr char kAppId[] = "views-shell";

// GetServerInformation's version. The bench copy of shell/ carries no git
// metadata, so the build has no commit stamp yet; this names the chapter.
constexpr char kNotificationServerVersion[] = "views-shell chapter 2";

// The --demo-workspace-switch target, by name.
constexpr char kDemoWorkspace[] = "3";

// Menu command target for the --demo-popup menu. The items do nothing: the
// demo proves the xdg_popup path, not command handling.
class DemoMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  void ExecuteCommand(int command_id, int event_flags) override {}
  bool IsCommandIdEnabled(int command_id) const override { return true; }
};

DemoMenuDelegate& GetDemoMenuDelegate() {
  static DemoMenuDelegate delegate;
  return delegate;
}

// The compositor's i3-ipc socket, from the environment only: $SCROLLSOCK,
// then $SWAYSOCK, then $I3SOCK, the first that names an existing file. Empty
// when none does (the UI thread does not run `scroll --get-socketpath`).
std::string ResolveCompositorSocket() {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  for (const char* name : {"SCROLLSOCK", "SWAYSOCK", "I3SOCK"}) {
    std::optional<std::string> path = env->GetVar(name);
    if (path && !path->empty()) {
      if (base::PathExists(base::FilePath(*path))) {
        return *path;
      }
      LOG(WARNING) << "wm: $" << name << " names " << *path
                   << ", which does not exist";
    }
  }
  return std::string();
}

std::string_view CloseReasonName(notifications::CloseReason reason) {
  switch (reason) {
    case notifications::CloseReason::kExpired:
      return "expired";
    case notifications::CloseReason::kDismissedByUser:
      return "dismissed";
    case notifications::CloseReason::kClosedByCall:
      return "closed-by-call";
    case notifications::CloseReason::kUndefined:
      return "undefined";
  }
  return "unknown";
}

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
  params.name = std::string(spec.layer_namespace);
  params.wayland_app_id = kAppId;
  params.wm_class_name = kAppId;
  params.wm_class_class = kAppId;
  widget->Init(std::move(params));
  widget->Show();
  return widget;
}

// --left-tabs: the ported workspace strip on the left, a content area in the
// theme's ground on the right. The strip fills when the source answers.
class LeftTabsContents : public views::View {
 public:
  explicit LeftTabsContents(std::unique_ptr<WorkspaceSource> source)
      : source_(std::move(source)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    strip_ = AddChildView(std::make_unique<WorkspaceStrip>(
        base::BindRepeating([](const Workspace& workspace) {
          LOG(INFO) << "left-tabs: selected workspace " << workspace.id << " ("
                    << base::UTF16ToUTF8(workspace.title) << ")";
        })));
    auto* content = AddChildView(std::make_unique<views::View>());
    content->SetBackground(views::CreateSolidBackground(ui::kColorSysBase));
    layout->SetFlexForView(content, 1);
    source_->Fetch(base::BindOnce(&LeftTabsContents::OnWorkspaces,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  void OnWorkspaces(std::vector<Workspace> workspaces) {
    std::string active = "none";
    for (const Workspace& workspace : workspaces) {
      if (workspace.active) {
        active = base::UTF16ToUTF8(workspace.title);
      }
    }
    LOG(INFO) << "left-tabs: " << workspaces.size() << " workspaces from "
              << source_->name() << ", active " << active;
    strip_->SetWorkspaces(std::move(workspaces));
  }

  std::unique_ptr<WorkspaceSource> source_;
  raw_ptr<WorkspaceStrip> strip_ = nullptr;
  base::WeakPtrFactory<LeftTabsContents> weak_ptr_factory_{this};
};

}  // namespace

// The delegate of the main surface (the bar or the left tabs). Closing it
// ends the program.
class ShellContent::SurfaceDelegate : public views::WidgetDelegate {
 public:
  SurfaceDelegate(std::unique_ptr<views::View> contents,
                  base::RepeatingClosure on_close)
      : on_close_(std::move(on_close)) {
    SetTitle(u"views-shell");
    SetContentsView(std::move(contents));
  }

  void set_before_close(base::OnceClosure before_close) {
    before_close_ = std::move(before_close);
  }

  // views::WidgetDelegate:
  void WindowClosing() override {
    if (before_close_) {
      std::move(before_close_).Run();
    }
    if (on_close_) {
      on_close_.Run();
    }
  }

 private:
  base::OnceClosure before_close_;
  base::RepeatingClosure on_close_;
};

ShellContentParams::ShellContentParams() = default;
ShellContentParams::ShellContentParams(ShellContentParams&&) = default;
ShellContentParams& ShellContentParams::operator=(ShellContentParams&&) =
    default;
ShellContentParams::~ShellContentParams() = default;

ShellContent::ShellContent(ShellContentParams params,
                           base::RepeatingClosure on_close)
    : params_(std::move(params)), on_close_(std::move(on_close)) {
  CHECK_NE(params_.bar, params_.left_tabs);
  CHECK(params_.bar || !(params_.demo_popup || params_.demo_workspace_switch ||
                         params_.demo_keyboard));
  CHECK(params_.left_tabs == static_cast<bool>(params_.workspace_source));
}

ShellContent::~ShellContent() {
  Stop();
}

void ShellContent::Start() {
  CHECK(!started_);
  started_ = true;
  StartTheme();
  if (params_.bar) {
    StartCompositor();
    StartNotifications();
  }
  StartSurfaces();
}

void ShellContent::Stop() {
  if (!started_) {
    return;
  }
  started_ = false;
  weak_ptr_factory_.InvalidateWeakPtrs();

  // 4. Surfaces: the menu's nested loop first, then the modal, then the main
  // surface (whose WindowClosing would run on_close_; it is cleared first).
  CancelDemoPopup();
  if (modal_widget_) {
    modal_widget_->CloseNow();
    modal_widget_.reset();
  }
  modal_delegate_.reset();
  if (main_widget_) {
    on_close_.Reset();
    bar_ = nullptr;
    main_widget_->CloseNow();
    main_widget_.reset();
  }
  main_delegate_.reset();
  menu_runner_.reset();
  menu_model_.reset();

  // 3. Notifications.
  if (notifications_) {
    if (notifications_->server()) {
      notifications_->server()->RemoveObserver(this);
    }
    notifications_->Stop();
    notifications_.reset();
  }

  // 2. The model, then its adapter (which joins its IO thread).
  if (model_) {
    model_->RemoveObserver(this);
    model_.reset();
  }
  adapter_.reset();
  // 1. The theme stays applied until the process ends; nothing draws now.
}

void ShellContent::StartTheme() {
  // Chrome's tab colours, in terms of the stock kColorSys* ids (tabs/).
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddTabColorMixer));
  if (!params_.theme) {
    return;
  }
  LOG(INFO) << "theme: " << params_.theme_dir.value() << " mode "
            << (params_.theme->color_mode ==
                        ui::ColorProviderKey::ColorMode::kDark
                    ? "dark"
                    : "light")
            << " seed " << ToHexColor(params_.theme->seed) << ", "
            << params_.theme->pins.size() << " kColorSys pins";
  theme_controller_.Apply(std::move(*params_.theme),
                          ui::NativeTheme::GetInstanceForNativeUi());
  params_.theme.reset();
}

void ShellContent::StartCompositor() {
  const std::string socket = ResolveCompositorSocket();
  if (socket.empty()) {
    LOG(WARNING) << "wm: no compositor socket ($SCROLLSOCK, $SWAYSOCK and "
                    "$I3SOCK unset); the bar runs without workspaces";
    return;
  }
  LOG(INFO) << "wm: scroll adapter on " << socket;
  scroll::ScrollAdapter::Options options;
  options.socket_path = socket;
  auto adapter = std::make_unique<scroll::ScrollAdapter>(options);
  model_ = std::make_unique<WmModel>(adapter.get());
  adapter_ = std::move(adapter);
  model_->AddObserver(this);
  adapter_->Start(model_.get());
}

void ShellContent::StartNotifications() {
  if (!base::Environment::Create()->HasVar("DBUS_SESSION_BUS_ADDRESS")) {
    LOG(WARNING) << "notifications: no session bus "
                    "($DBUS_SESSION_BUS_ADDRESS unset); the notification "
                    "daemon is off";
    return;
  }
  notifications_ = std::make_unique<notifications::NotificationService>(
      kNotificationServerVersion);
  notifications_->Start(
      /*use_session_bus=*/true, base::BindOnce([](bool owned) {
        if (owned) {
          LOG(INFO) << "notifications: serving org.freedesktop.Notifications";
        } else {
          LOG(WARNING) << "notifications: org.freedesktop.Notifications not "
                          "owned (another daemon?)";
        }
      }));
  notifications_->server()->AddObserver(this);
}

void ShellContent::StartSurfaces() {
  std::unique_ptr<views::View> contents;
  const SurfaceSpec* spec = nullptr;
  if (params_.bar) {
    spec = FindSurfaceSpec("bar");
    auto bar = std::make_unique<BarView>(base::DefaultClock::GetInstance());
    bar_ = bar.get();
    if (model_) {
      bar->SetLeftView(std::make_unique<bar::WorkspaceStrip>(model_.get()));
    }
    if (params_.demo_popup || params_.demo_workspace_switch ||
        params_.demo_keyboard) {
      bar->SetFirstPaintCallback(base::BindOnce(
          &ShellContent::OnBarFirstPaint, weak_ptr_factory_.GetWeakPtr()));
    }
    contents = std::move(bar);
  } else {
    spec = FindSurfaceSpec("left-tabs");
    contents =
        std::make_unique<LeftTabsContents>(std::move(params_.workspace_source));
  }
  CHECK(spec);
  main_delegate_ =
      std::make_unique<SurfaceDelegate>(std::move(contents), on_close_);
  main_delegate_->set_before_close(base::BindOnce(
      &ShellContent::CancelDemoPopup, weak_ptr_factory_.GetWeakPtr()));
  main_widget_ = CreateSurfaceWidget(*spec, main_delegate_.get());
  if (model_ && model_->has_snapshot()) {
    OnSnapshotApplied();
  }
}

void ShellContent::OnBarFirstPaint() {
  // The demos start one second after the bar's first painted frame (SPEC.md
  // C4.3 for the popup).
  auto task_runner = base::SingleThreadTaskRunner::GetCurrentDefault();
  if (params_.demo_popup) {
    task_runner->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ShellContent::OpenDemoPopup,
                       weak_ptr_factory_.GetWeakPtr()),
        base::Seconds(1));
  }
  if (params_.demo_workspace_switch) {
    task_runner->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ShellContent::DemoWorkspaceSwitchDue,
                       weak_ptr_factory_.GetWeakPtr()),
        base::Seconds(1));
  }
  if (params_.demo_keyboard) {
    task_runner->PostTask(FROM_HERE,
                          base::BindOnce(&ShellContent::OpenKeyboardProbe,
                                         weak_ptr_factory_.GetWeakPtr()));
  }
}

// Opens the demo menu from the bar. Runs a nested (nestable) message loop
// until the menu closes; headless runs kill the process instead.
void ShellContent::OpenDemoPopup() {
  if (!main_widget_ || menu_runner_) {
    return;
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(&GetDemoMenuDelegate());
  menu_model_->AddItem(0, u"views-shell demo item");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(1, u"second demo item");
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::NO_FLAGS);
  const int bar_height = FindSurfaceSpec("bar")->height;
  menu_runner_->RunMenuAt(main_widget_.get(),
                          /*button_controller=*/nullptr,
                          gfx::Rect(16, 0, 120, bar_height),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

void ShellContent::CancelDemoPopup() {
  if (menu_runner_ && menu_runner_->IsRunning()) {
    menu_runner_->Cancel();
  }
}

void ShellContent::DemoWorkspaceSwitchDue() {
  if (!model_) {
    LOG(ERROR) << "demo-workspace-switch: no compositor adapter; nothing to "
                  "switch";
    return;
  }
  switch_due_ = true;
  MaybeSendDemoWorkspaceSwitch();
}

void ShellContent::MaybeSendDemoWorkspaceSwitch() {
  if (!switch_due_ || switch_sent_ || !model_->has_snapshot()) {
    return;
  }
  switch_sent_ = true;
  LOG(INFO) << "demo-workspace-switch: FocusWorkspace " << kDemoWorkspace;
  // By name: the workspace need not exist yet (scroll creates it), so there
  // is no id to pass to WmModel::FocusWorkspace.
  model_->FocusWorkspaceByName(
      kDemoWorkspace,
      base::BindOnce([](base::expected<void, WmCommandError> result) {
        if (result.has_value()) {
          LOG(INFO) << "demo-workspace-switch: command landed";
        } else {
          LOG(ERROR) << "demo-workspace-switch: command failed: "
                     << WmCommandErrorName(result.error());
        }
      }));
}

void ShellContent::OpenKeyboardProbe() {
  if (modal_widget_) {
    return;
  }
  const SurfaceSpec* spec = FindSurfaceSpec("modal");
  CHECK(spec);
  modal_delegate_ = std::make_unique<views::WidgetDelegate>();
  modal_delegate_->SetTitle(u"views-shell keyboard probe");
  auto* probe =
      modal_delegate_->SetContentsView(std::make_unique<KeyboardProbeView>());
  modal_delegate_->SetInitiallyFocusedView(probe->textfield());
  LOG(INFO) << "keyboard-probe: opening the modal surface";
  modal_widget_ = CreateSurfaceWidget(*spec, modal_delegate_.get());
}

void ShellContent::OnFocusChanged(const std::string& workspace,
                                  const std::string& window) {
  const WmWorkspace* focused = model_->snapshot().FocusedWorkspace();
  LOG(INFO) << "wm: focused workspace "
            << (focused ? focused->name : std::string("none"));
}

void ShellContent::OnSnapshotApplied() {
  // The centre title follows the focused window (rule R6: from the model).
  if (bar_) {
    const WmWindow* window = model_->snapshot().FocusedWindow();
    bar_->SetTitle(window ? base::UTF8ToUTF16(window->title)
                          : std::u16string());
  }
  MaybeSendDemoWorkspaceSwitch();
  if (switch_sent_ && !switch_echoed_) {
    const WmWorkspace* focused = model_->snapshot().FocusedWorkspace();
    if (focused && focused->name == kDemoWorkspace) {
      switch_echoed_ = true;
      LOG(INFO) << "ECHO workspace " << kDemoWorkspace;
    }
  }
}

void ShellContent::OnNotificationClosed(uint32_t id,
                                        notifications::CloseReason reason) {
  LOG(INFO) << "notifications: " << id << " closed ("
            << CloseReasonName(reason) << ")";
}

}  // namespace views_shell
