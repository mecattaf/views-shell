// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Everything views-shell runs above the process bootstrap
// (app/shell_bootstrap.h), as one object with an explicit Start and Stop.
// views_shell_main.cc parses the flags, brings the bootstrap up, then starts
// this; it never builds a surface itself.
//
// Start() order, and Stop() in reverse:
//   1. theme: Chrome's tab colours (tabs/tab_color_mixer.h) and, with
//      --theme, the resolved theme on the native theme and the pin mixer
//      (style/theme_mixer.h), before the first widget;
//   2. the compositor: with --bar, or with --left-tabs fed by the compositor
//      (--workspace-source=scroll), a ScrollAdapter (wm/adapters/scroll) on
//      the socket $SCROLLSOCK, $SWAYSOCK or $I3SOCK names, feeding a WmModel.
//      Without a socket the program runs on, says so once, and the strip has
//      no workspaces;
//   3. notifications: with --bar, when $DBUS_SESSION_BUS_ADDRESS is set, the
//      freedesktop notification daemon (notifications/notification_service.h),
//      whose popups are their own layer surfaces ("notification" SurfaceSpec);
//   4. surfaces: the bar (BarView with the WorkspaceStrip in its left section,
//      the focused window's title in the centre and the clock on the right)
//      or, with --left-tabs, the ported tab strip (tabs/workspace_strip.h)
//      filling the "left-tabs" surface, fed by the WmModel through
//      tabs/workspace_strip_model_binding.h or by a one-shot WorkspaceSource;
//      with --demo-keyboard also the "modal" surface holding the
//      KeyboardProbeView.
//
// Rule R6: the bar and the strip redraw from WmModel observer callbacks only.
// ShellContent observes the model for the centre title and for
// --demo-workspace-switch, which one second after the bar's first paint (or
// after the left-tabs surface is shown) asks the model to focus the workspace
// named "3" and logs "ECHO workspace 3" when the model reports it focused.

#ifndef VIEWS_SHELL_APP_SHELL_CONTENT_H_
#define VIEWS_SHELL_APP_SHELL_CONTENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "views_shell/notifications/notification_server.h"
#include "views_shell/style/theme_mixer.h"
#include "views_shell/wm/wm_model.h"

namespace views {
class MenuRunner;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace ui {
class SimpleMenuModel;
}

namespace views_shell {

class BarView;
class CompositorAdapter;
class WorkspaceSource;

namespace notifications {
class NotificationService;
}

struct ShellContentParams {
  ShellContentParams();
  ShellContentParams(ShellContentParams&&);
  ShellContentParams& operator=(ShellContentParams&&);
  ~ShellContentParams();

  // Exactly one of the two surfaces.
  bool bar = false;
  bool left_tabs = false;
  // Demos (bar only, except demo_workspace_switch, which both surfaces run).
  bool demo_popup = false;
  bool demo_workspace_switch = false;
  bool demo_keyboard = false;
  // The theme, resolved by main before the bootstrap; unset: stock colours.
  std::optional<ResolvedTheme> theme;
  base::FilePath theme_dir;
  // --left-tabs only: exactly one of the two. A one-shot source (static,
  // niri), or the compositor adapter's WmModel (--workspace-source=scroll).
  std::unique_ptr<WorkspaceSource> workspace_source;
  bool left_tabs_from_compositor = false;
};

class ShellContent : public WmModel::Observer,
                     public notifications::NotificationServer::Observer {
 public:
  // `on_close` runs when the main surface closes (the program then ends).
  ShellContent(ShellContentParams params, base::RepeatingClosure on_close);
  ShellContent(const ShellContent&) = delete;
  ShellContent& operator=(const ShellContent&) = delete;
  ~ShellContent() override;  // Stops if still started.

  // UI thread, after ShellBootstrap::Init().
  void Start();
  // UI thread, before the bootstrap goes. Ends a running demo menu first.
  void Stop();

  // Ends the --demo-popup menu's nested loop, if it runs, so the caller's
  // run loop can quit.
  void CancelDemoPopup();

  // WmModel::Observer:
  void OnFocusChanged(const std::string& workspace,
                      const std::string& window) override;
  void OnSnapshotApplied() override;

  // notifications::NotificationServer::Observer:
  void OnNotificationClosed(
      uint32_t id,
      notifications::CloseReason reason) override;

 private:
  class SurfaceDelegate;

  void StartTheme();
  void StartCompositor();
  void StartNotifications();
  void StartSurfaces();

  // The bar's first paint: arms the demos that wait for it.
  void OnBarFirstPaint();
  void OpenDemoPopup();
  void DemoWorkspaceSwitchDue();
  void MaybeSendDemoWorkspaceSwitch();
  void OpenKeyboardProbe();

  ShellContentParams params_;
  base::RepeatingClosure on_close_;
  bool started_ = false;

  // 1.
  ThemeController theme_controller_;
  // 2.
  std::unique_ptr<CompositorAdapter> adapter_;
  std::unique_ptr<WmModel> model_;
  // 3.
  std::unique_ptr<notifications::NotificationService> notifications_;
  // 4.
  std::unique_ptr<SurfaceDelegate> main_delegate_;
  std::unique_ptr<views::Widget> main_widget_;
  raw_ptr<BarView> bar_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> modal_delegate_;
  std::unique_ptr<views::Widget> modal_widget_;

  // --demo-popup.
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // --demo-workspace-switch: due (one second after the first paint), sent,
  // echoed.
  bool switch_due_ = false;
  bool switch_sent_ = false;
  bool switch_echoed_ = false;

  base::WeakPtrFactory<ShellContent> weak_ptr_factory_{this};
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_APP_SHELL_CONTENT_H_
