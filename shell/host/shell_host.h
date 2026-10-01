// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/shell_host.h
//
// agency::ShellHost -- the browser-process singleton that owns the shell's
// layer-shell surfaces. For Surface 1 that is exactly one surface: the top bar
// (a views::Widget named "agency:bar", welded onto wlr-layer-shell by the
// name-keyed dispatch in desktop_window_tree_host_linux.cc).
//
// Constructed from ShellBrowserMainExtraParts::PostBrowserStart() under
// --agency-layer-shell (by which point Ozone/Views/aura and the message loop
// are up), destroyed from PostMainMessageLoopRun(). See topbar-design.md
// sections 3.1 and 3.5.

#ifndef AGENCY_SHELL_SHELL_HOST_H_
#define AGENCY_SHELL_SHELL_HOST_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace views {
class Widget;
}  // namespace views

namespace agency {

class BarView;
class ClockController;
class ClockProducer;
class LauncherPanel;
class LauncherProducer;
class RailController;
class RailView;

// Owns the shell's surfaces + their producer/controller bridges for one
// browser process. Single instance, held by ShellBrowserMainExtraParts.
class ShellHost {
 public:
  ShellHost();  // builds the ClockProducer + bar Widget + BarView + controller
  ShellHost(const ShellHost&) = delete;
  ShellHost& operator=(const ShellHost&) = delete;
  ~ShellHost();  // tears down controller -> widget -> producer (see .cc)

 private:
  void BuildBar();  // creates the Widget, names it "agency:bar", shows it
  void BuildLauncher();  // creates+names+shows the "agency:launcher" widget
  void BuildRail();  // Surface 5 v1: creates+names+shows "agency:rail" + mirror

  scoped_refptr<dbus::Bus> system_bus_;  // holds GetSystemBus() alive
  std::unique_ptr<ClockProducer> clock_producer_;  // SingleOwner<ClockProducer>
  std::unique_ptr<views::Widget> bar_widget_;      // CLIENT_OWNS_WIDGET
  std::unique_ptr<ClockController> clock_controller_;  // owns observer receiver
  raw_ptr<BarView> bar_view_ = nullptr;  // owned by bar_widget_'s ContentsView

  // Surface 3: the launcher / command palette. Built only under
  // --agency-launcher. No observer/mojo seam -- the panel drives the producer
  // by direct call (LauncherPanel IS its own controller).
  std::unique_ptr<LauncherProducer> launcher_producer_;  // SingleOwner guard
  std::unique_ptr<views::Widget> launcher_widget_;       // CLIENT_OWNS_WIDGET
  raw_ptr<LauncherPanel> launcher_view_ = nullptr;       // owned by the widget

  // Surface 5 v1: the left rail (read-only mirror). Built only under
  // --agency-rail. The controller owns the niri IPC seam + fold; the view is
  // owned by rail_widget_'s ContentsView. Teardown order: controller (stops the
  // niri IO thread + drops its raw_ptr<RailView>) BEFORE the widget frees the
  // view -- see ~ShellHost.
  std::unique_ptr<RailController> rail_controller_;
  std::unique_ptr<views::Widget> rail_widget_;      // CLIENT_OWNS_WIDGET
  raw_ptr<RailView> rail_view_ = nullptr;           // owned by the widget
};

}  // namespace agency

#endif  // AGENCY_SHELL_SHELL_HOST_H_
