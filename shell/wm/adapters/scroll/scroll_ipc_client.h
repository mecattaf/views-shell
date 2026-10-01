// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SKETCH. Never compiled. See README.md in this directory.

#ifndef VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_
#define VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_

#include <cstdint>
#include <string>

#include "base/files/scoped_file.h"
#include "base/message_loop/message_pump_for_io.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views-shell::scroll {

// From scroll/include/ipc.h (i3/sway numbering, plus scroll's own).
enum class IpcType : uint32_t {
  kRunCommand = 0,
  kGetWorkspaces = 1,
  kSubscribe = 2,
  kGetOutputs = 3,
  kGetTree = 4,
  kGetVersion = 7,
  kGetConfig = 9,
  kSendTick = 10,
  kGetScroller = 120,  // scroll only
  kGetTrails = 121,    // scroll only
  kGetSpaces = 122,    // scroll only
  kGetBindings = 123,  // scroll only
  // 124 (LUA_EVAL) is deliberately absent from the core client.
};

enum class IpcEvent : uint32_t {
  kWorkspace = 0x80000000,
  kOutput = 0x80000001,
  kMode = 0x80000002,
  kWindow = 0x80000003,
  kBinding = 0x80000005,
  kShutdown = 0x80000006,
  kTick = 0x80000007,
  kScroller = 0x8000001e,  // scroll only
  kTrails = 0x8000001f,    // scroll only
};

class ScrollIpcClient : public CompositorAdapter {
 public:
  // Resolves the socket as scroll's common/ipc-client.c does.
  static std::string ResolveSocketPath();

  explicit ScrollIpcClient(std::string socket_path);
  ~ScrollIpcClient() override;

  // CompositorAdapter:
  std::string_view name() const override;  // "scroll" or "sway", from GET_VERSION
  const CapabilitySet& capabilities() const override;
  void Start(Delegate* delegate) override;
  void Send(const WmCommand& command, CommandDone done) override;

 private:
  class Connection;  // one framed socket with its FdWatcher
  std::unique_ptr<Connection> events_;
  std::unique_ptr<Connection> requests_;
  CapabilitySet capabilities_;
  raw_ptr<Delegate> delegate_ = nullptr;
};

}  // namespace views-shell::scroll

#endif  // VIEWS_SHELL_WM_ADAPTERS_SCROLL_SCROLL_IPC_CLIENT_H_
