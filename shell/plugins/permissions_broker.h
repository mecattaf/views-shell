// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The permissions broker (docs/architecture.md §6, rule R11): every request a
// plugin makes of views-shell passes here and is checked against the
// permissions its manifest declares. An undeclared request is answered with
// JSON-RPC error -32001 "permission not declared" and never ends the plugin.
//
//   notify              needs `notifications`; handed to the Client
//   toast               needs nothing; handed to the Client
//   exec                needs `exec:<program>`; the broker runs the program
//                       itself (base::LaunchProcess, PATH lookup, stdin from
//                       /dev/null, a deadline) and answers
//                       {exitCode, stdout, stderr}
//   compositor/command  needs `compositor:<capability>`; mapped to a typed
//                       WmCommand and sent through a WmCommandSink, never as a
//                       raw compositor string; answered {} once the
//                       compositor's echo arrived
//
// The broker also answers the two questions the host asks without a request:
// may this plugin's tree or handler call another plugin's command (`call:`),
// and may it read another plugin's source (`state:read:`).

#ifndef VIEWS_SHELL_PLUGINS_PERMISSIONS_BROKER_H_
#define VIEWS_SHELL_PLUGINS_PERMISSIONS_BROKER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "views_shell/plugins/json_rpc.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell {

struct PluginManifest;

struct PluginNotification {
  PluginNotification();
  PluginNotification(const PluginNotification&);
  PluginNotification(PluginNotification&&);
  PluginNotification& operator=(const PluginNotification&);
  PluginNotification& operator=(PluginNotification&&);
  ~PluginNotification();

  std::string summary;
  std::string body;
  std::string icon;
  std::string urgency = "normal";  // low, normal, critical
};

struct PluginToast {
  PluginToast();
  PluginToast(const PluginToast&);
  PluginToast(PluginToast&&);
  PluginToast& operator=(const PluginToast&);
  PluginToast& operator=(PluginToast&&);
  ~PluginToast();

  std::string text;
  std::string icon;
};

// Where typed compositor commands go. The assembly binds it to the running
// CompositorAdapter (whose Send() has this signature); tests bind a fake.
class WmCommandSink {
 public:
  virtual ~WmCommandSink() = default;
  virtual void Send(const WmCommand& command,
                    CompositorAdapter::CommandDone done) = 0;
};

struct ExecOutcome {
  ExecOutcome();
  ExecOutcome(const ExecOutcome&);
  ExecOutcome(ExecOutcome&&);
  ExecOutcome& operator=(const ExecOutcome&);
  ExecOutcome& operator=(ExecOutcome&&);
  ~ExecOutcome();

  // {exitCode, stdout, stderr}
  base::DictValue ToDict() const;

  int exit_code = 0;  // 127 when the program could not be started, 137 when
                      // the deadline killed it
  std::string stdout_text;
  std::string stderr_text;
};

class PermissionsBroker {
 public:
  class Client {
   public:
    virtual ~Client() = default;
    virtual void OnNotify(const PluginManifest& plugin,
                          const PluginNotification& notification) = 0;
    virtual void OnToast(const PluginManifest& plugin,
                         const PluginToast& toast) = 0;
  };

  using Reply = base::OnceCallback<void(JsonRpcResult)>;

  static constexpr base::TimeDelta kExecDeadline = base::Seconds(10);
  static constexpr size_t kExecOutputCap = 1024 * 1024;

  // `client` must outlive the broker; `wm` may be null (compositor/command is
  // then answered -32000 "no compositor").
  PermissionsBroker(Client* client, WmCommandSink* wm);
  PermissionsBroker(const PermissionsBroker&) = delete;
  PermissionsBroker& operator=(const PermissionsBroker&) = delete;
  ~PermissionsBroker();

  void set_wm_command_sink(WmCommandSink* wm) { wm_ = wm; }

  // Answers one plugin request. `reply` runs exactly once, possibly later
  // (exec, compositor/command), never synchronously for those two.
  void HandleRequest(const PluginManifest& plugin,
                     const std::string& method,
                     const base::DictValue& params,
                     Reply reply);

  // The permission a plugin request needs; nullopt when it needs none (toast)
  // or the method is not a plugin request.
  static std::optional<std::string> PermissionFor(
      std::string_view method,
      const base::DictValue& params);

  // A tree action or a T1 `call` handler naming another plugin's command.
  static bool AllowsCall(const PluginManifest& plugin,
                         std::string_view qualified_command);
  // `source/changed` for <plugin-id>/<source>: the plugin's own sources, or a
  // declared `state:read:<plugin-id>/<source>`.
  static bool AllowsStateRead(const PluginManifest& plugin,
                              std::string_view qualified_source);

  // capability + args -> the typed command. The error says what is missing.
  static base::expected<WmCommand, std::string> ToWmCommand(
      std::string_view capability,
      const base::DictValue& args);

  // Runs `program` (looked up on PATH) with `args`, blocking: call it on a
  // sequence that may block and wait (the broker posts it to the thread pool).
  static ExecOutcome RunProgram(const std::string& program,
                                const std::vector<std::string>& args,
                                base::TimeDelta deadline);

  // Posts RunProgram() to the thread pool and answers with its outcome.
  static void RunProgramAsync(const std::string& program,
                              const std::vector<std::string>& args,
                              base::OnceCallback<void(ExecOutcome)> done);

 private:
  raw_ptr<Client> client_;
  raw_ptr<WmCommandSink> wm_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_PERMISSIONS_BROKER_H_
