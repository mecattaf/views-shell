// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A T2 plugin: a child process that speaks the plugin protocol
// (schemas/plugin-protocol.md, protocol 1) as newline-delimited JSON-RPC 2.0 on
// its stdin and stdout. ProcessPlugin launches runtime.exec with the plugin
// directory as its working directory, performs initialize, routes what the
// plugin sends (trees and snapshots to a TreeSink, requests to the
// PermissionsBroker), delivers what views-shell sends, restarts the process
// with exponential backoff when it exits on its own, and shuts it down within
// the grace period.
//
// Launch path. When `systemd-run` is on PATH and a probe
// (`systemd-run --user --scope --quiet --collect -- true`) succeeds, every
// launch runs inside its own transient scope unit
// (views-shell-plugin-<id>-<pid>-<n>.scope), so a failure is visible to the
// unit-failure tripwire. Otherwise the process is started with plain
// base::LaunchProcess. Inside runtime-test (a private /run/user) there is no
// reachable user manager, so the plain path is taken; the bench's FHS build
// environment does reach the user manager, so a kAuto launch there takes the
// scope path. Tests that need one path force it with Options::launch_path.
// The path taken is logged at every launch and readable through
// launch_path().
//
// Threading. A ProcessPlugin lives on one sequence that supports
// base::FileDescriptorWatcher (a MessagePumpType::IO thread). Blocking work
// (the launch-path probe, waiting for an exit) runs on the thread pool.

#ifndef VIEWS_SHELL_PLUGINS_PROCESS_PLUGIN_H_
#define VIEWS_SHELL_PLUGINS_PROCESS_PLUGIN_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "views_shell/plugins/json_rpc.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell {

class PermissionsBroker;
class TreeSink;
struct PluginManifest;

inline constexpr char kShellVersion[] = "0.1.0";

class ProcessPlugin {
 public:
  enum class LaunchPath { kAuto, kScope, kPlain };
  enum class State {
    kStopped,       // never started
    kStarting,      // launched, initialize not answered yet
    kRunning,       // initialize answered with protocol 1
    kRestarting,    // exited on its own; a restart is scheduled
    kFailed,        // given up: too many crashes, or a protocol mismatch
    kShuttingDown,  // shutdown sent; waiting for the exit
    kExited,        // shut down
  };

  struct Options {
    Options();
    Options(const Options&) = delete;
    Options(Options&&);
    Options& operator=(const Options&) = delete;
    Options& operator=(Options&&);
    ~Options();

    LaunchPath launch_path = LaunchPath::kAuto;
    // The protocol's windows: startup (trees and a snapshot after
    // initialize), the answer deadline of every request, and the shutdown
    // grace period.
    base::TimeDelta startup_window = base::Seconds(2);
    base::TimeDelta request_timeout = base::Seconds(2);
    base::TimeDelta shutdown_grace = base::Seconds(1);
    // Restart backoff: initial_backoff doubles per consecutive crash up to
    // max_backoff; after max_restarts consecutive crashes the plugin is
    // kFailed. A run that lasted stable_after resets the count.
    base::TimeDelta initial_backoff = base::Milliseconds(500);
    base::TimeDelta max_backoff = base::Seconds(30);
    int max_restarts = 5;
    base::TimeDelta stable_after = base::Seconds(30);
    // initialize params.
    std::string shell_version = kShellVersion;
    std::string locale = "en-US";
    CapabilitySet capabilities;  // the host's; narrowed to what the plugin
                                 // declares
    base::DictValue config;      // user values; declared defaults fill the rest
  };

  // What the host shows for a plugin that exited on its own.
  struct Crash {
    Crash();
    Crash(const Crash&);
    Crash(Crash&&);
    Crash& operator=(const Crash&);
    Crash& operator=(Crash&&);
    ~Crash();

    std::string plugin_id;
    int exit_code = -1;  // -1: killed by a signal, or never started
    std::string reason;  // "exited", "cannot launch", "no initialize answer",
                         // "protocol mismatch"
    int consecutive = 0;
    std::optional<base::TimeDelta> restart_in;  // nullopt: kFailed
    std::string stderr_tail;
  };

  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void OnPluginRunning(const ProcessPlugin& plugin) {}
    virtual void OnPluginCrashed(const ProcessPlugin& plugin,
                                 const Crash& crash) = 0;
  };

  using ResultCallback = base::OnceCallback<void(JsonRpcResult)>;

  // `manifest`, `sink`, `broker` and `observer` must outlive this object.
  ProcessPlugin(const PluginManifest& manifest,
                Options options,
                TreeSink* sink,
                PermissionsBroker* broker,
                Observer* observer);
  ProcessPlugin(const ProcessPlugin&) = delete;
  ProcessPlugin& operator=(const ProcessPlugin&) = delete;
  // Kills a process that is still running (no grace: call Shutdown first).
  ~ProcessPlugin();

  void Start();

  // command/invoke. `command` is the local or qualified id of a declared
  // command without a verb; args are checked against the declared args
  // before anything is sent (-32602 otherwise). The callback gets the
  // plugin's answer, checked against the command's result type.
  void Invoke(std::string_view command,
              base::DictValue args,
              std::string_view source,
              std::optional<std::string> instance,
              ResultCallback callback);
  void SurfaceOpened(const std::string& surface,
                     const std::string& instance,
                     ResultCallback callback);
  void SurfaceClosed(const std::string& surface,
                     const std::string& instance,
                     ResultCallback callback);
  void ConfigChanged(base::DictValue config, ResultCallback callback);
  // Returns false (and sends nothing) for a name contributes.events does not
  // declare.
  bool DeliverEvent(const std::string& name, base::Value data);
  // Returns false unless the broker allows state:read on `source`.
  bool DeliverSourceChanged(const std::string& source, base::Value data);

  // shutdown, then close stdin; the process must exit within the grace
  // period or it is killed. `done` gets the exit status (-1 when killed).
  void Shutdown(base::OnceCallback<void(int exit_code)> done);

  const PluginManifest& manifest() const { return *manifest_; }
  State state() const { return state_; }
  // Running, initialized, and not being reaped.
  bool IsAlive() const;
  // The path the latest launch took (kScope or kPlain); kAuto before one.
  LaunchPath launch_path() const { return launch_path_; }
  base::ProcessId pid() const { return pid_; }
  int launches() const { return launches_; }
  // The full config initialize and config/changed carry.
  base::DictValue EffectiveConfig() const;

  static std::string_view LaunchPathName(LaunchPath path);
  static std::string_view StateName(State state);

 private:
  struct Pending {
    Pending();
    Pending(Pending&&);
    Pending& operator=(Pending&&);
    ~Pending();

    std::string method;
    ResultCallback callback;
    std::unique_ptr<base::OneShotTimer> timer;
  };
  struct Queued {
    Queued();
    Queued(Queued&&);
    Queued& operator=(Queued&&);
    ~Queued();

    std::string method;
    base::DictValue params;
    ResultCallback callback;
  };

  void Launch(LaunchPath path);
  void OnLaunchPathKnown(LaunchPath path);
  void OnStdoutReadable();
  void OnStderrReadable();
  void OnWritable();
  void Write(std::string line);
  void FlushWrites();
  void HandleLine(const std::string& line);
  void HandleAnswer(JsonRpcMessage message);
  void HandleNotification(JsonRpcMessage message);
  void HandleRequest(JsonRpcMessage message);
  void OnInitializeAnswer(JsonRpcResult result);
  void OnStartupWindowEnd();
  void SendRequest(std::string method,
                   base::DictValue params,
                   base::TimeDelta timeout,
                   ResultCallback callback);
  // Sends now when running; queues while starting; fails otherwise.
  void Call(std::string method, base::DictValue params, ResultCallback cb);
  void SendNotification(std::string method, base::DictValue params);
  void OnRequestTimeout(const std::string& id);
  void ReplyToPlugin(base::Value id, JsonRpcResult result);
  void OnStreamsClosed();
  // Moves the process to the thread pool, waits `grace` for its exit, kills
  // it otherwise, then calls OnExited.
  void Reap(base::TimeDelta grace, std::string reason);
  void OnExited(std::string reason, int exit_code);
  void FailPending(const std::string& why);
  void CloseStdin();
  void KillNow();
  void SetState(State state);

  const raw_ref<const PluginManifest> manifest_;
  Options options_;
  const raw_ptr<TreeSink> sink_;
  const raw_ptr<PermissionsBroker> broker_;
  const raw_ptr<Observer> observer_;

  State state_ = State::kStopped;
  LaunchPath launch_path_ = LaunchPath::kAuto;
  base::Process process_;
  base::ProcessId pid_ = base::kNullProcessId;
  int launches_ = 0;
  int consecutive_crashes_ = 0;
  bool reaping_ = false;
  bool protocol_mismatch_ = false;
  std::string fail_reason_;
  base::TimeTicks launched_at_;

  base::ScopedFD stdin_;
  base::ScopedFD stdout_;
  base::ScopedFD stderr_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdout_watch_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stderr_watch_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdin_watch_;
  std::string write_buffer_;
  JsonRpcLineReader stdout_reader_;
  JsonRpcLineReader stderr_reader_;
  std::string stderr_tail_;

  int next_request_ = 0;
  std::map<std::string, Pending> pending_;
  std::vector<Queued> queued_;
  std::vector<std::string> trees_seen_;
  bool snapshot_seen_ = false;
  base::OneShotTimer startup_timer_;
  base::OneShotTimer restart_timer_;
  base::OnceCallback<void(int)> shutdown_done_;

  base::WeakPtrFactory<ProcessPlugin> weak_factory_{this};
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_PROCESS_PLUGIN_H_
