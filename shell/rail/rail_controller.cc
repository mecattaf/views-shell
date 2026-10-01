// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/rail_controller.cc

#include "agency/shell/rail_controller.h"

#include <algorithm>
#include <utility>

#include "agency/shell/rail_view.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/bind_post_task.h"

namespace agency {

RailController::RailController(RailView* rail_view,
                              std::string niri_socket_path)
    : rail_view_(rail_view),
      niri_socket_path_(std::move(niri_socket_path)),
      io_thread_("agency_rail_niri_io"),
      niri_client_(nullptr, base::OnTaskRunnerDeleter(nullptr)) {}

RailController::~RailController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // niri_client_'s OnTaskRunnerDeleter posts the real destruction to
  // io_thread_'s runner; Stop() then joins after that task runs. Both are
  // no-ops when the live path was never taken (null client, un-started thread).
  niri_client_.reset();
  if (io_thread_started_) {
    io_thread_.Stop();
  }
}

void RailController::StartLiveMirror() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (io_thread_started_) {
    return;
  }

  // NiriIpcClient's FdWatchController requires a MessagePumpType::IO sequence
  // (signal/niri/niri_ipc_client.h); this dedicated thread is that sequence.
  base::Thread::Options options(base::MessagePumpType::IO, /*size=*/0);
  io_thread_started_ = io_thread_.StartWithOptions(std::move(options));
  if (!io_thread_started_) {
    LOG(ERROR) << "[agency-rail] failed to start niri IO thread; rail stays "
                  "empty";
    return;
  }

  // Built HERE, on the origin (UI) sequence: weak_factory_.GetWeakPtr() and
  // base::BindPostTaskToCurrentDefault() are both only valid on this sequence.
  // Each wrapped callback re-posts to this sequence before the real On*()
  // method runs, so the fold state needs no locking (same recipe as
  // WindowProvider::Initialize).
  niri::NiriEventCallbacks callbacks;
  callbacks.on_workspaces_changed =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWorkspacesChanged, weak_factory_.GetWeakPtr()));
  callbacks.on_windows_changed =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWindowsChanged, weak_factory_.GetWeakPtr()));
  callbacks.on_workspace_activated =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWorkspaceActivated, weak_factory_.GetWeakPtr()));
  callbacks.on_window_focus_changed =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWindowFocusChanged, weak_factory_.GetWeakPtr()));
  callbacks.on_window_opened_or_changed =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWindowOpenedOrChanged,
          weak_factory_.GetWeakPtr()));
  callbacks.on_window_closed =
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &RailController::OnWindowClosed, weak_factory_.GetWeakPtr()));

  // base::Unretained is safe: base::Thread::Stop()'s contract runs all pending
  // tasks to completion before joining, and ~RailController calls Stop() only
  // while `this` is otherwise fully alive (same argument as WindowProvider).
  io_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&RailController::CreateAndStartClientOnIoThread,
                     base::Unretained(this), std::move(callbacks)));
  LOG(ERROR) << "[agency-rail] live niri mirror starting (socket='"
             << niri_socket_path_ << "')";
}

void RailController::CreateAndStartClientOnIoThread(
    niri::NiriEventCallbacks callbacks) {
  // Runs ON io_thread_; NiriIpcClient is constructed and lives here per its
  // SEQUENCE_CHECKER.
  niri_client_ =
      std::unique_ptr<niri::NiriIpcClient, base::OnTaskRunnerDeleter>(
          new niri::NiriIpcClient(niri_socket_path_, std::move(callbacks)),
          base::OnTaskRunnerDeleter(io_thread_.task_runner()));
  niri_client_->Start();
}

void RailController::InjectTestSnapshot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TEST-ONLY: this is synthetic data fed through the exact same fold path the
  // live niri client uses. It exists so headless-eval (which has no niri) can
  // still prove the rail's render pipeline end to end. NEVER runs under a live
  // --agency-rail without --agency-rail-test-snapshot.
  LOG(ERROR) << "[agency-rail] TEST-ONLY static snapshot injected (no niri): "
                "3 workspaces / 4 windows through the live fold path";

  niri::WorkspacesChanged wsc;
  auto mk_ws = [](uint64_t id, uint8_t idx, const char* name, bool active,
                  bool urgent) {
    niri::Workspace w;
    w.id = id;
    w.idx = idx;
    w.name = std::string(name);
    w.is_active = active;
    w.is_urgent = urgent;
    return w;
  };
  wsc.workspaces.push_back(mk_ws(1, 1, "term", /*active=*/true, false));
  wsc.workspaces.push_back(mk_ws(2, 2, "web", false, false));
  wsc.workspaces.push_back(mk_ws(3, 3, "chat", false, /*urgent=*/true));
  OnWorkspacesChanged(wsc);

  niri::WindowsChanged wc;
  auto mk_win = [](uint64_t id, uint64_t ws_id, const char* app_id,
                   const char* title, bool focused) {
    niri::Window w;
    w.id = id;
    w.workspace_id = ws_id;
    w.app_id = std::string(app_id);
    w.title = std::string(title);
    w.is_focused = focused;
    return w;
  };
  wc.windows.push_back(mk_win(101, 1, "kitty", "nvim src/agency", true));
  wc.windows.push_back(mk_win(102, 1, "kitty", "ninja -C out/agency", false));
  wc.windows.push_back(mk_win(201, 2, "firefox", "issue #10 — the rail", false));
  wc.windows.push_back(mk_win(301, 3, "signal", "agency-agency", false));
  OnWindowsChanged(wc);
}

void RailController::OnWorkspacesChanged(const niri::WorkspacesChanged& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  workspaces_ = event.workspaces;
  Rerender();
}

void RailController::OnWindowsChanged(const niri::WindowsChanged& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  windows_ = event.windows;
  Rerender();
}

void RailController::OnWorkspaceActivated(
    const niri::WorkspaceActivated& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // niri's WorkspaceActivated names the newly-active workspace on its output;
  // exactly one workspace per output is active. We do not track outputs in v1,
  // so mark the named one active and clear the rest (single-output boxes are
  // the common case; multi-output refinement is deferred with the rest of the
  // interaction work).
  for (niri::Workspace& w : workspaces_) {
    w.is_active = (w.id == event.id);
  }
  Rerender();
}

void RailController::OnWindowFocusChanged(
    const niri::WindowFocusChanged& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (niri::Window& w : windows_) {
    w.is_focused = event.id.has_value() && w.id == *event.id;
  }
  Rerender();
}

void RailController::OnWindowOpenedOrChanged(
    const niri::WindowOpenedOrChanged& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = std::find_if(
      windows_.begin(), windows_.end(),
      [&](const niri::Window& w) { return w.id == event.window.id; });
  if (it != windows_.end()) {
    *it = event.window;
  } else {
    windows_.push_back(event.window);
  }
  Rerender();
}

void RailController::OnWindowClosed(const niri::WindowClosed& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase_if(windows_,
                [&](const niri::Window& w) { return w.id == event.id; });
  Rerender();
}

void RailController::Rerender() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!rail_view_) {
    return;
  }

  // Stable order: by niri's 1-based per-output idx, then id as a tiebreak so a
  // resync never reshuffles same-idx rows.
  std::vector<niri::Workspace> ordered = workspaces_;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const niri::Workspace& a, const niri::Workspace& b) {
                     if (a.idx != b.idx) {
                       return a.idx < b.idx;
                     }
                     return a.id < b.id;
                   });

  RailModel model;
  for (const niri::Workspace& ws : ordered) {
    RailWorkspaceItem item;
    item.label = ws.name.value_or("ws " + base::NumberToString(ws.idx));
    item.is_active = ws.is_active;
    item.is_urgent = ws.is_urgent;

    for (const niri::Window& win : windows_) {
      if (!win.workspace_id.has_value() || *win.workspace_id != ws.id) {
        continue;
      }
      RailWindowItem w;
      w.label = win.title.value_or(
          win.app_id.value_or("window " + base::NumberToString(win.id)));
      w.is_focused = win.is_focused;
      item.windows.push_back(std::move(w));
    }
    model.workspaces.push_back(std::move(item));
  }

  rail_view_->SetModel(model);
}

}  // namespace agency
