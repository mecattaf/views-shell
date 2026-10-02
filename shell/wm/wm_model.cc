// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/wm/wm_model.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/notreached.h"

namespace views_shell {

// --- wm_snapshot.h and compositor_adapter.h special members ----------------

WmOutput::WmOutput() = default;
WmOutput::WmOutput(const WmOutput&) = default;
WmOutput::WmOutput(WmOutput&&) = default;
WmOutput& WmOutput::operator=(const WmOutput&) = default;
WmOutput& WmOutput::operator=(WmOutput&&) = default;
WmOutput::~WmOutput() = default;
bool WmOutput::operator==(const WmOutput&) const = default;

WmWorkspace::WmWorkspace() = default;
WmWorkspace::WmWorkspace(const WmWorkspace&) = default;
WmWorkspace::WmWorkspace(WmWorkspace&&) = default;
WmWorkspace& WmWorkspace::operator=(const WmWorkspace&) = default;
WmWorkspace& WmWorkspace::operator=(WmWorkspace&&) = default;
WmWorkspace::~WmWorkspace() = default;
bool WmWorkspace::operator==(const WmWorkspace&) const = default;

WmWindow::WmWindow() = default;
WmWindow::WmWindow(const WmWindow&) = default;
WmWindow::WmWindow(WmWindow&&) = default;
WmWindow& WmWindow::operator=(const WmWindow&) = default;
WmWindow& WmWindow::operator=(WmWindow&&) = default;
WmWindow::~WmWindow() = default;
bool WmWindow::operator==(const WmWindow&) const = default;

WmSnapshot::WmSnapshot() = default;
WmSnapshot::WmSnapshot(const WmSnapshot&) = default;
WmSnapshot::WmSnapshot(WmSnapshot&&) = default;
WmSnapshot& WmSnapshot::operator=(const WmSnapshot&) = default;
WmSnapshot& WmSnapshot::operator=(WmSnapshot&&) = default;
WmSnapshot::~WmSnapshot() = default;
bool WmSnapshot::operator==(const WmSnapshot&) const = default;

const WmWorkspace* WmSnapshot::FindWorkspace(const std::string& id) const {
  for (const WmWorkspace& workspace : workspaces) {
    if (workspace.id == id) {
      return &workspace;
    }
  }
  return nullptr;
}

const WmWindow* WmSnapshot::FindWindow(const std::string& id) const {
  for (const WmWindow& window : windows) {
    if (window.id == id) {
      return &window;
    }
  }
  return nullptr;
}

const WmWorkspace* WmSnapshot::FocusedWorkspace() const {
  for (const WmWorkspace& workspace : workspaces) {
    if (workspace.focused) {
      return &workspace;
    }
  }
  return nullptr;
}

const WmWindow* WmSnapshot::FocusedWindow() const {
  for (const WmWindow& window : windows) {
    if (window.focused) {
      return &window;
    }
  }
  return nullptr;
}

WmCommand::WmCommand() = default;
WmCommand::WmCommand(Kind kind) : kind(kind) {}
WmCommand::WmCommand(const WmCommand&) = default;
WmCommand::WmCommand(WmCommand&&) = default;
WmCommand& WmCommand::operator=(const WmCommand&) = default;
WmCommand& WmCommand::operator=(WmCommand&&) = default;
WmCommand::~WmCommand() = default;

CompositorAdapter::Delegate::JumpLabel::JumpLabel() = default;
CompositorAdapter::Delegate::JumpLabel::JumpLabel(const JumpLabel&) = default;
CompositorAdapter::Delegate::JumpLabel::JumpLabel(JumpLabel&&) = default;
CompositorAdapter::Delegate::JumpLabel&
CompositorAdapter::Delegate::JumpLabel::operator=(const JumpLabel&) = default;
CompositorAdapter::Delegate::JumpLabel&
CompositorAdapter::Delegate::JumpLabel::operator=(JumpLabel&&) = default;
CompositorAdapter::Delegate::JumpLabel::~JumpLabel() = default;

Capability RequiredCapability(WmCommand::Kind kind) {
  switch (kind) {
    case WmCommand::Kind::kFocusWorkspace:
      return "workspaces.focus";
    case WmCommand::Kind::kFocusWindow:
      return "windows.focus";
    case WmCommand::Kind::kRenameWorkspace:
      return "workspaces.rename";
    case WmCommand::Kind::kMoveWindowToWorkspace:
      return "windows.move-to-workspace";
    case WmCommand::Kind::kToggleScratchpad:
      return "scratchpad.toggle";
    case WmCommand::Kind::kSessionExit:
      return "session.exit";
    case WmCommand::Kind::kReloadConfig:
      return "config.reload";
  }
  NOTREACHED();
}

std::string_view WmCommandErrorName(WmCommandError error) {
  switch (error) {
    case WmCommandError::kNotConnected:
      return "not-connected";
    case WmCommandError::kCapabilityMissing:
      return "capability-missing";
    case WmCommandError::kInvalidArgument:
      return "invalid-argument";
    case WmCommandError::kRejected:
      return "rejected";
    case WmCommandError::kNoEcho:
      return "no-echo";
  }
  NOTREACHED();
}

// --- WmModel -----------------------------------------------------------------

namespace {

// True when `range` holds `value`.
template <typename Range, typename T>
bool Contains(const Range& range, const T& value) {
  return std::ranges::find(range, value) != std::ranges::end(range);
}

using Children = std::map<std::string, std::vector<std::string>>;

std::string ParentOf(const WmWindow& window) {
  return window.workspace.empty() ? std::string(WmModel::kScratchpadParent)
                                  : window.workspace;
}

Children ChildrenOf(const WmSnapshot& snapshot) {
  Children children;
  std::vector<std::string>& root = children[std::string()];
  for (const WmWorkspace& workspace : snapshot.workspaces) {
    root.push_back(workspace.id);
  }
  for (const WmWindow& window : snapshot.windows) {
    children[ParentOf(window)].push_back(window.id);
  }
  return children;
}

// A window as compared by OnItemChanged: its parent is the tree's business.
WmWindow WithoutParent(WmWindow window) {
  window.workspace.clear();
  return window;
}

}  // namespace

WmModel::WmModel(CompositorAdapter* adapter) : adapter_(adapter) {
  CHECK(adapter_);
}

WmModel::~WmModel() = default;

void WmModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void WmModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

const CapabilitySet& WmModel::capabilities() const {
  return adapter_->capabilities();
}

void WmModel::OnSnapshot(WmSnapshot snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const WmSnapshot old = std::move(current_);
  current_ = std::move(snapshot);
  const WmSnapshot& now = current_;

  Children working = ChildrenOf(old);
  const Children target = ChildrenOf(now);
  const std::string root;

  // 1. Windows that are gone.
  for (const WmWindow& window : old.windows) {
    if (!now.FindWindow(window.id)) {
      const std::string parent = ParentOf(window);
      std::erase(working[parent], window.id);
      observers_.Notify(&Observer::OnChildRemoved, parent, window.id);
    }
  }

  // Brings working[parent] to target[parent], which holds only ids that are
  // already in working[parent] or are new to the tree.
  auto reconcile = [&](const std::string& parent) {
    std::vector<std::string>& list = working[parent];
    auto it = target.find(parent);
    if (it == target.end()) {
      return;
    }
    const std::vector<std::string>& want = it->second;
    for (size_t i = 0; i < want.size(); ++i) {
      const std::string& id = want[i];
      if (i < list.size() && list[i] == id) {
        continue;
      }
      const int index = static_cast<int>(i);
      if (Contains(list, id)) {
        std::erase(list, id);
        list.insert(list.begin() + std::min(i, list.size()), id);
        observers_.Notify(&Observer::OnChildMoved, parent, parent, id, index);
      } else {
        list.insert(list.begin() + std::min(i, list.size()), id);
        observers_.Notify(&Observer::OnChildAdded, parent, id, index);
      }
    }
  };

  // 2. Workspaces added and reordered; the ones going away end up last.
  reconcile(root);

  // 3a. Windows that changed parent, appended to their new parent.
  for (const WmWindow& window : now.windows) {
    const WmWindow* before = old.FindWindow(window.id);
    if (!before) {
      continue;
    }
    const std::string from = ParentOf(*before);
    const std::string to = ParentOf(window);
    if (from == to) {
      continue;
    }
    std::erase(working[from], window.id);
    working[to].push_back(window.id);
    observers_.Notify(&Observer::OnChildMoved, from, to, window.id,
                      static_cast<int>(working[to].size()) - 1);
  }

  // 3b. Windows added and reordered within each parent.
  for (const WmWorkspace& workspace : now.workspaces) {
    reconcile(workspace.id);
  }
  reconcile(std::string(kScratchpadParent));

  // 4. Workspaces that are gone (empty by now).
  for (const WmWorkspace& workspace : old.workspaces) {
    if (!now.FindWorkspace(workspace.id)) {
      std::erase(working[root], workspace.id);
      observers_.Notify(&Observer::OnChildRemoved, root, workspace.id);
    }
  }

  // 5. Items that stayed but changed.
  for (const WmWorkspace& workspace : now.workspaces) {
    const WmWorkspace* before = old.FindWorkspace(workspace.id);
    if (before && !(*before == workspace)) {
      observers_.Notify(&Observer::OnItemChanged, workspace.id);
    }
  }
  for (const WmWindow& window : now.windows) {
    const WmWindow* before = old.FindWindow(window.id);
    if (before && !(WithoutParent(*before) == WithoutParent(window))) {
      observers_.Notify(&Observer::OnItemChanged, window.id);
    }
  }
  if (old.outputs != now.outputs) {
    observers_.Notify(&Observer::OnOutputsChanged);
  }
  if (old.mru != now.mru) {
    observers_.Notify(&Observer::OnMruChanged);
  }

  // 6. Focus.
  auto focus = [](const WmSnapshot& s) {
    const WmWorkspace* workspace = s.FocusedWorkspace();
    const WmWindow* window = s.FocusedWindow();
    return std::make_pair(workspace ? workspace->id : std::string(),
                          window ? window->id : std::string());
  };
  const auto old_focus = focus(old);
  const auto new_focus = focus(now);
  if (!has_snapshot_ || old_focus != new_focus) {
    observers_.Notify(&Observer::OnFocusChanged, new_focus.first,
                      new_focus.second);
  }

  has_snapshot_ = true;
  observers_.Notify(&Observer::OnSnapshotApplied);
}

void WmModel::OnBinding(std::string_view payload) {
  observers_.Notify(&Observer::OnBinding, payload);
}

void WmModel::OnConfigReloaded(bool ok, std::string_view error) {
  observers_.Notify(&Observer::OnConfigReloaded, ok, error);
}

void WmModel::OnDisconnected() {
  observers_.Notify(&Observer::OnDisconnected);
}

void WmModel::FocusWorkspace(const std::string& workspace,
                             CompositorAdapter::CommandDone done) {
  WmCommand command(WmCommand::Kind::kFocusWorkspace);
  command.workspace = workspace;
  Send(std::move(command), std::move(done));
}

void WmModel::FocusWorkspaceByName(const std::string& name,
                                   CompositorAdapter::CommandDone done) {
  WmCommand command(WmCommand::Kind::kFocusWorkspace);
  command.name = name;
  Send(std::move(command), std::move(done));
}

void WmModel::FocusWindow(const std::string& window,
                          CompositorAdapter::CommandDone done) {
  WmCommand command(WmCommand::Kind::kFocusWindow);
  command.window = window;
  Send(std::move(command), std::move(done));
}

void WmModel::RenameWorkspace(const std::string& workspace,
                              const std::string& name,
                              CompositorAdapter::CommandDone done) {
  WmCommand command(WmCommand::Kind::kRenameWorkspace);
  command.workspace = workspace;
  command.name = name;
  Send(std::move(command), std::move(done));
}

void WmModel::MoveWindow(const std::string& window,
                         const std::string& workspace,
                         CompositorAdapter::CommandDone done) {
  WmCommand command(WmCommand::Kind::kMoveWindowToWorkspace);
  command.window = window;
  command.workspace = workspace;
  Send(std::move(command), std::move(done));
}

void WmModel::ToggleScratchpad(CompositorAdapter::CommandDone done) {
  Send(WmCommand(WmCommand::Kind::kToggleScratchpad), std::move(done));
}

void WmModel::ReloadConfig(CompositorAdapter::CommandDone done) {
  Send(WmCommand(WmCommand::Kind::kReloadConfig), std::move(done));
}

void WmModel::ExitSession(CompositorAdapter::CommandDone done) {
  Send(WmCommand(WmCommand::Kind::kSessionExit), std::move(done));
}

void WmModel::Send(WmCommand command, CompositorAdapter::CommandDone done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!done) {
    done = base::BindOnce([](base::expected<void, WmCommandError> result) {
      LOG_IF(WARNING, !result.has_value())
          << "wm: command failed: " << WmCommandErrorName(result.error());
    });
  }
  adapter_->Send(command, std::move(done));
}

}  // namespace views_shell
