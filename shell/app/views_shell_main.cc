// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// views-shell: a content-free Views program on Ozone/Wayland. No //content, no
// Blink, no V8. The process bootstrap (single-process Ozone, mojo, the
// in-process viz host, aura::Env, the ViewsDelegate, wm::WMState) is
// app/shell_bootstrap.h; everything above it (theme, compositor adapter and
// WmModel, notifications, surfaces) is app/shell_content.h. This file keeps
// the flags.
//
// --software-compositing (the default) or --gpu-compositing picks how the
// in-process viz draws (app/views_shell_context_factory.h).
//
// views-shell draws only on layer surfaces (rule R1): every top-level widget
// is created on a registered SurfaceSpec (app/surface_spec.h), and the
// ViewsDelegate CHECKs it. There is no window mode: without --bar or
// --left-tabs the program says so and exits 2.
//
// --bar puts the bar on the "bar" surface: top layer, anchored to the top
// edge, 32 px high, exclusive zone, namespace "views-shell-bar". Its left
// section is the workspace strip of the compositor's WmModel (bar/
// workspace_strip.h, when $SCROLLSOCK/$SWAYSOCK resolves), its centre the
// focused window's title, its right the clock. With --bar the notification
// daemon runs too when a session bus is reachable; its popups are layer
// surfaces of their own ("views-shell-notification").
//
// Demos, with --bar, each one second after the bar's first paint unless said:
//   --demo-popup              a views::MenuRunner menu, an xdg_popup parented
//                             onto the layer surface via get_popup;
//   --demo-workspace-switch   WmModel focuses the workspace named "3"; the
//                             model's echo logs "ECHO workspace 3";
//   --demo-keyboard           at once, the "modal" surface (overlay, centred,
//                             exclusive keyboard) with a views::Textfield that
//                             logs "TYPED <text>" (app/keyboard_probe_view.h).
//
// --left-tabs puts the workspace strip (Chrome's tab, ported under tabs/; one
// tab per workspace) on the "left-tabs" surface, a rail on the left edge with
// an exclusive zone. --workspace-source=scroll|static|niri picks the
// workspaces: scroll is the compositor adapter over i3-ipc (the strip follows
// its snapshots and a click asks it to focus the workspace, rule R6); static
// and niri answer once. Without the flag: scroll when $SCROLLSOCK, $SWAYSOCK
// or $I3SOCK is set, else niri when $NIRI_SOCKET is set, else static.
// --demo-workspace-switch works here as on the bar.
//
// --theme <dir> (or --theme=<dir>) wears an Omarchy theme directory (rule R17,
// style/): it is read and resolved before anything else and applied before the
// first widget. Without --theme the colours are stock ui/color.
//
// --run-for-seconds=<n> ends the program after n seconds with exit code 0,
// through ShellContent::Stop() and the bootstrap's teardown.

#include <optional>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "views_shell/app/shell_bootstrap.h"
#include "views_shell/app/shell_content.h"
#include "views_shell/app/views_shell_context_factory.h"
#include "views_shell/style/theme_mixer.h"
#include "views_shell/tabs/workspace_source.h"

namespace views_shell {
namespace {

constexpr char kRunForSeconds[] = "run-for-seconds";
constexpr char kBar[] = "bar";
constexpr char kDemoPopup[] = "demo-popup";
constexpr char kDemoWorkspaceSwitch[] = "demo-workspace-switch";
constexpr char kDemoKeyboard[] = "demo-keyboard";
constexpr char kLeftTabs[] = "left-tabs";
constexpr char kWorkspaceSource[] = "workspace-source";
constexpr char kSoftwareCompositing[] = "software-compositing";
constexpr char kGpuCompositing[] = "gpu-compositing";
constexpr char kTheme[] = "theme";

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

  ShellContentParams content_params;
  content_params.bar = command_line->HasSwitch(kBar);
  // The demos run from the bar; without --bar there is none (SPEC.md C4.3).
  content_params.demo_popup =
      content_params.bar && command_line->HasSwitch(kDemoPopup);
  content_params.demo_keyboard =
      content_params.bar && command_line->HasSwitch(kDemoKeyboard);
  // The workspace strip is its own surface, not the bar's contents.
  content_params.left_tabs =
      !content_params.bar && command_line->HasSwitch(kLeftTabs);
  // The switch demo runs from either surface.
  content_params.demo_workspace_switch =
      command_line->HasSwitch(kDemoWorkspaceSwitch);
  if (!content_params.bar && !content_params.left_tabs) {
    // Rule R1: no xdg_toplevel, so no window mode. Refused before anything
    // connects to the compositor. (Chapter 1's C3.4 window run is superseded.)
    LOG(ERROR) << "views-shell draws only on layer surfaces (rule R1); pass "
                  "--bar (or --left-tabs)";
    return 2;
  }

  // The theme is read and resolved before the process comes up, so a bad
  // theme directory costs no compositor connection.
  if (command_line->HasSwitch(kTheme)) {
    // --theme=<dir>, or --theme <dir> (base::CommandLine leaves a value after
    // a space as the first positional argument).
    base::FilePath theme_dir = command_line->GetSwitchValuePath(kTheme);
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
    content_params.theme = std::move(resolved).value();
    content_params.theme_dir = theme_dir;
  }
  if (content_params.left_tabs) {
    std::string source_name =
        command_line->GetSwitchValueASCII(kWorkspaceSource);
    if (source_name.empty()) {
      auto env = base::Environment::Create();
      source_name = env->HasVar("SCROLLSOCK") || env->HasVar("SWAYSOCK") ||
                            env->HasVar("I3SOCK")
                        ? "scroll"
                        : "";
    }
    if (source_name == "scroll") {
      content_params.left_tabs_from_compositor = true;
    } else {
      content_params.workspace_source = CreateWorkspaceSource(source_name);
      if (!content_params.workspace_source) {
        LOG(ERROR) << "--" << kWorkspaceSource
                   << " wants scroll, static or niri, not " << source_name;
        return 2;
      }
    }
  }

  ShellBootstrap bootstrap(bootstrap_params);
  if (!bootstrap.Init()) {
    return 1;
  }

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  ShellContent content(std::move(content_params), run_loop.QuitClosure());
  content.Start();

  if (run_for_seconds > 0) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](ShellContent* content, base::OnceClosure quit) {
              content->CancelDemoPopup();
              std::move(quit).Run();
            },
            &content, run_loop.QuitClosure()),
        base::Seconds(run_for_seconds));
  }
  run_loop.Run();

  // Above the bootstrap first (surfaces, notifications, the adapter), then
  // the bootstrap tears the process down in reverse when it goes out of scope.
  content.Stop();
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
