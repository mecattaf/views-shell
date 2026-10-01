// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/shell/rail_controller.h
//
// agency::RailController -- the bridge that drives RailView (Surface 5, v1
// read-only mirror) from niri's live compositor state. It owns a NiriIpcClient
// on a dedicated MessagePumpType::IO thread (the same threading recipe
// WindowProvider uses, producers/launcher/window_provider.cc), folds the
// WorkspacesChanged / WindowsChanged full-resyncs and the per-object deltas
// (WorkspaceActivated / WindowFocusChanged / WindowOpenedOrChanged /
// WindowClosed) into a retained model, and re-projects that model into a
// RailModel it pushes to the view on the UI/origin sequence.
//
// Callbacks land back on the origin (UI) sequence via
// base::BindPostTaskToCurrentDefault, so the fold state (workspaces_ / windows_)
// and the RailView touch happen with no locking, exactly like WindowProvider.
//
// HEADLESS / test path: InjectTestSnapshot() feeds a canned WorkspacesChanged +
// WindowsChanged through the SAME fold entrypoints (OnWorkspacesChanged /
// OnWindowsChanged) the live client would call. It starts no thread and opens
// no socket -- headless-eval has no niri -- so the rendered rail is real code on
// synthetic, clearly-logged TEST-ONLY data.

#ifndef AGENCY_SHELL_RAIL_CONTROLLER_H_
#define AGENCY_SHELL_RAIL_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "agency/signal/niri/niri_events.h"
#include "agency/signal/niri/niri_ipc_client.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"

namespace agency {

class RailView;

class RailController {
 public:
  // `rail_view` is owned by the rail Widget and MUST outlive this controller
  // (ShellHost tears the controller down before the widget). `niri_socket_path`
  // is $NIRI_SOCKET; used only by StartLiveMirror().
  RailController(RailView* rail_view, std::string niri_socket_path);
  RailController(const RailController&) = delete;
  RailController& operator=(const RailController&) = delete;
  ~RailController();

  // Bring up the IO thread + NiriIpcClient and begin mirroring live niri state.
  // No-op-safe to call once. On a box with no reachable niri the client's own
  // backoff loop simply keeps retrying; the rail stays empty until niri answers.
  void StartLiveMirror();

  // Render a canned workspace/window snapshot through the live fold path. For
  // headless-eval (no niri). Clearly logged TEST-ONLY. Does NOT start a thread.
  void InjectTestSnapshot();

 private:
  // Runs ON io_thread_: constructs + Starts the NiriIpcClient there, per its
  // SEQUENCE_CHECKER.
  void CreateAndStartClientOnIoThread(niri::NiriEventCallbacks callbacks);

  // Fold handlers -- all run on the origin (UI) sequence. Each mutates the
  // retained vectors then Rerender()s.
  void OnWorkspacesChanged(const niri::WorkspacesChanged& event);
  void OnWindowsChanged(const niri::WindowsChanged& event);
  void OnWorkspaceActivated(const niri::WorkspaceActivated& event);
  void OnWindowFocusChanged(const niri::WindowFocusChanged& event);
  void OnWindowOpenedOrChanged(const niri::WindowOpenedOrChanged& event);
  void OnWindowClosed(const niri::WindowClosed& event);

  // Project workspaces_ + windows_ into a RailModel and push to the view.
  void Rerender();

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<RailView> rail_view_;
  const std::string niri_socket_path_;

  // Retained niri state, folded from the event stream (or the test snapshot).
  std::vector<niri::Workspace> workspaces_;
  std::vector<niri::Window> windows_;

  // The dedicated IO sequence the NiriIpcClient lives on. Only started by
  // StartLiveMirror(); left un-started on the test path.
  base::Thread io_thread_;
  bool io_thread_started_ = false;

  // OnTaskRunnerDeleter posts destruction back to io_thread_ before Stop()
  // joins it, so the client always dies on its own sequence.
  std::unique_ptr<niri::NiriIpcClient, base::OnTaskRunnerDeleter> niri_client_;

  base::WeakPtrFactory<RailController> weak_factory_{this};
};

}  // namespace agency

#endif  // AGENCY_SHELL_RAIL_CONTROLLER_H_
