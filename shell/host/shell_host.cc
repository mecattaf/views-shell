// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/shell_host.cc

#include "agency/shell/shell_host.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agency/dbus/bus.h"
#include "agency/producers/clock/clock_producer.h"
#include "agency/producers/launcher/launcher_producer.h"
#include "agency/shell/bar_view.h"
#include "agency/shell/clock_controller.h"
#include "agency/shell/launcher_panel.h"
#include "agency/shell/rail_controller.h"
#include "agency/shell/rail_view.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/nix/xdg_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"

namespace agency {

ShellHost::ShellHost() {
  // Hold the shared system bus alive for the ClockProducer's lifetime and
  // construct the producer on it (it begins async timedate1 bring-up). Bus law:
  // never dbus::Bus::Create -- always the shared accessor.
  system_bus_ = agency::GetSystemBus();
  clock_producer_ = std::make_unique<ClockProducer>(system_bus_.get());

  BuildBar();

  // Surface 3 is an additive, opt-in demo affordance: build the launcher only
  // when explicitly asked. --agency-layer-shell (required for the weld table to
  // fire) is already true by the time ShellHost is constructed; --agency-launcher
  // is the strict subset gate that stands up the palette.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("agency-launcher")) {
    BuildLauncher();
  }

  // Surface 5 v1 (issue #10): the left rail read-only mirror. Additive, opt-in,
  // same gating shape as the launcher. --agency-rail stands up the surface + the
  // niri mirror; the strict-subset --agency-rail-test-snapshot feeds a canned
  // workspace/window snapshot instead of connecting to niri (headless-eval).
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("agency-rail")) {
    BuildRail();
  }
}

ShellHost::~ShellHost() {
  // Teardown order (topbar-design.md section 3.5, corrected):
  //  1. clock_controller_ FIRST -> unbinds the mojo::Receiver and stops the
  //     timer, then destroys the controller's raw_ptr<views::Label> label_.
  //     This must precede bar_widget_.reset(): the Label is owned by the
  //     widget's BarView, so destroying the widget first would free the Label
  //     while label_ still referenced it -- a dangling raw_ptr that trips
  //     BackupRefPtr's dangling-pointer detector (enabled in dcheck/debug
  //     builds) at free time and aborts shutdown. Controller-before-widget
  //     drops that reference before the Label dies.
  clock_controller_.reset();
  //  2. bar_widget_ (CLIENT_OWNS_WIDGET) -> Close/destroy stops paint/layout
  //     and releases the layer surface via WaylandLayerShellWindow::Hide, so
  //     bar_view_ (owned by the widget) dies here.
  bar_view_ = nullptr;
  bar_widget_.reset();
  //  3. clock_producer_ -> releases the SingleOwner<ClockProducer> guard. It
  //     outlives the controller (reset above), so the observer receiver is
  //     already unbound by the time the producer goes away.
  clock_producer_.reset();
  //  4. Launcher chain (independent of the bar/clock chain, and simpler: there
  //     is NO mojo::Receiver to unbind -- the launcher has no observer seam).
  //     raw_ptr dangling-safe: the LauncherPanel holds a
  //     raw_ptr<LauncherProducer>, so the widget (which owns the panel) must
  //     die before the producer. Clear the raw_ptr, drop the widget (frees the
  //     LauncherPanel and its raw_ptr<LauncherProducer>), then the producer.
  launcher_view_ = nullptr;
  launcher_widget_.reset();
  //  5. Producer last: its SingleOwner<LauncherProducer> guard releases after
  //     no view references it.
  launcher_producer_.reset();
  //  6. Rail chain. The RailController holds a raw_ptr<RailView> and owns a
  //     niri IO thread whose folded callbacks touch that view. Reset the
  //     controller FIRST: its destructor stops the IO thread (no more folds
  //     land) and invalidates its weak-ptr-bound callbacks, so no posted fold
  //     can dereference the view after this. THEN drop the widget (which frees
  //     the RailView). Controller-before-widget keeps the raw_ptr non-dangling.
  rail_controller_.reset();
  rail_view_ = nullptr;
  rail_widget_.reset();
  // system_bus_ is a process-global refcounted bus -- NOT torn down here; the
  // scoped_refptr simply drops its reference.
}

void ShellHost::BuildBar() {
  // CLIENT_OWNS_WIDGET: we hold the std::unique_ptr<views::Widget> and control
  // its lifetime. TYPE_WINDOW_FRAMELESS: a bar strip has no frame and must not
  // create a NonClientView — Widget::SetContentsView() DCHECKs on any type
  // where RequiresNonClientView() holds (widget.cc:804-811 at 150).
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  // The weld key: the name-keyed dispatch table in
  // desktop_window_tree_host_linux.cc re-routes a widget named "agency:bar"
  // onto a wlr-layer-shell surface. Nothing downstream overwrites this name for
  // a plain (non-browser) Widget.
  params.name = "agency:bar";
  // Width is filled by the compositor (anchored Left|Right); height is the
  // client-chosen bar height, matching the weld table's exclusive_zone.
  params.bounds = gfx::Rect(0, 0, 1, kBarHeightDip);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;

  bar_widget_ = std::make_unique<views::Widget>();
  bar_widget_->Init(std::move(params));

  auto bar = std::make_unique<BarView>();
  bar_view_ = bar.get();
  bar_widget_->SetContentsView(std::move(bar));

  clock_controller_ = std::make_unique<ClockController>(
      clock_producer_.get(), bar_view_->clock_label());

  bar_widget_->Show();
}

void ShellHost::BuildLauncher() {
  // --- Producer bring-up (no bus; niri via subprocess; see launcher_producer.h).
  // The producer is synchronous/in-process with NO mojom seam, NO observer, and
  // NO snapshot (Route-B ruling) -- the panel binds it by direct virtual call.
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  // os.db under $XDG_STATE_HOME/agency (fallback ~/.local/state), matching
  // AppProvider's frecency store. GetXDGDirectory verified @150 xdg_util.h:95.
  base::FilePath db_path =
      base::nix::GetXDGDirectory(env.get(), "XDG_STATE_HOME", ".local/state")
          .Append("agency")
          .Append("os.db");
  // sql::Database::Open() creates the file but NOT parent directories; on a
  // fresh box ~/.local/state/agency/ is absent, OsDb::Open() fails, and
  // AppProvider's frecency DCHECK fires (observed live on worker-tb).
  base::CreateDirectory(db_path.DirName());
  std::string niri_socket = env->GetVar("NIRI_SOCKET").value_or(std::string());
  // Terminal-requiring apps: an empty prefix disables them for the MVP demo
  // (documented degraded behavior, app_provider.h). Wire {"xterm","-e"} later.
  std::vector<std::string> terminal_command;

  launcher_producer_ = std::make_unique<LauncherProducer>(
      std::move(db_path), std::move(niri_socket), std::move(terminal_command));
  launcher_producer_->Initialize();  // initial .desktop scan + FilePathWatchers

  // --- Widget (identical shape to BuildBar). CLIENT_OWNS_WIDGET +
  // TYPE_WINDOW_FRAMELESS: a floating palette has no NonClientView.
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  // The weld key: desktop_window_tree_host_linux.cc re-routes a widget named
  // "agency:launcher" onto a centered, fixed-size wlr-layer-shell overlay.
  params.name = "agency:launcher";
  // Fixed size (both axes anchor=None => the compositor centers the surface;
  // an unanchored axis MUST supply a non-zero size, so the widget bounds drive
  // set_size). Cross-referenced to the weld row -- keep in sync.
  params.bounds = gfx::Rect(0, 0, kLauncherPanelWidth, kLauncherPanelHeight);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;

  launcher_widget_ = std::make_unique<views::Widget>();
  launcher_widget_->Init(std::move(params));

  auto panel = std::make_unique<LauncherPanel>(launcher_producer_.get());
  launcher_view_ = panel.get();
  launcher_widget_->SetContentsView(std::move(panel));
  launcher_widget_->Show();
  // Mark the search field as this widget's focus target. The real keyboard
  // routing fix lives in the Ozone seam: WaylandLayerShellWindow now delivers
  // OnActivationChanged() to its delegate when the compositor grants the
  // surface keyboard focus (issue #8) -- layer surfaces are otherwise excluded
  // from the WaylandToplevelWindow activation path, so the widget was never
  // activated and KeyEvents were dropped before reaching the Textfield. With
  // activation delivered, the FocusController restores this stored focus on
  // keyboard enter. Activate() remains a platform no-op for layer shells but is
  // harmless; RequestFocus() records the field as the view to restore.
  launcher_widget_->Activate();
  launcher_view_->search()->RequestFocus();
}

void ShellHost::BuildRail() {
  // --- Widget (identical shape to BuildBar). CLIENT_OWNS_WIDGET +
  // TYPE_WINDOW_FRAMELESS: a rail strip has no NonClientView.
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  // The weld key: desktop_window_tree_host_linux.cc re-routes a widget named
  // "agency:rail" onto a left-anchored wlr-layer-shell surface that reserves
  // kRailWidthDip via exclusive_zone (patches/agency-layer-shell-weld.patch).
  params.name = "agency:rail";
  // Height is filled by the compositor (anchored Top|Bottom|Left); width is the
  // client-chosen rail width, matching the weld table's exclusive_zone.
  params.bounds = gfx::Rect(0, 0, kRailWidthDip, 1);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;

  rail_widget_ = std::make_unique<views::Widget>();
  rail_widget_->Init(std::move(params));

  auto rail = std::make_unique<RailView>();
  rail_view_ = rail.get();
  rail_widget_->SetContentsView(std::move(rail));

  std::unique_ptr<base::Environment> env = base::Environment::Create();
  std::string niri_socket = env->GetVar("NIRI_SOCKET").value_or(std::string());
  rail_controller_ =
      std::make_unique<RailController>(rail_view_, std::move(niri_socket));

  rail_widget_->Show();

  // Data source. --agency-rail-test-snapshot is the headless-eval path: no niri,
  // a canned snapshot rendered through the live fold. Otherwise mirror niri.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "agency-rail-test-snapshot")) {
    rail_controller_->InjectTestSnapshot();
  } else {
    rail_controller_->StartLiveMirror();
  }
}

}  // namespace agency
