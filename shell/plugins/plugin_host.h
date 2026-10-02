// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The plugin host: ties the registry, the permissions broker and the running
// plugin instances together. It activates plugins as their activation events
// say (onStartup at Start(); onCommand, onSurface and onEvent on first use),
// holds each plugin's latest snapshot and its surfaces' trees, and reports to
// one Delegate. The Delegate is the binding point for the next chapter's
// assembly: the ui_tree renderer takes OnTree/OnSnapshot, the notification
// service OnNotify/OnToast, and the surface host OnCrash (a badge on the
// plugin's surfaces). Drawing is never done here.
//
// T0 (builtin) plugins are registered but run by their own C++; T1 plugins
// load their ui files at activation; T2 plugins are supervised processes.
// Like ProcessPlugin, the host lives on one sequence that supports
// base::FileDescriptorWatcher.

#ifndef VIEWS_SHELL_PLUGINS_PLUGIN_HOST_H_
#define VIEWS_SHELL_PLUGINS_PLUGIN_HOST_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "views_shell/plugins/declarative_plugin.h"
#include "views_shell/plugins/json_rpc.h"
#include "views_shell/plugins/permissions_broker.h"
#include "views_shell/plugins/plugin_manifest.h"
#include "views_shell/plugins/process_plugin.h"

namespace views_shell {

class PluginRegistry;

class PluginHost : public TreeSink,
                   public PermissionsBroker::Client,
                   public ProcessPlugin::Observer,
                   public DeclarativePlugin::Host {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // A surface is ready to draw: its tree and the snapshot it binds to (the
    // plugin's latest; {} for a T1 plugin, whose trees bind to sources).
    // Called again whenever the tree changes.
    virtual void OnTree(const std::string& plugin,
                        const std::string& surface,
                        const base::DictValue& tree,
                        const base::DictValue& snapshot) = 0;
    // A new full snapshot for every drawn surface of `plugin`.
    virtual void OnSnapshot(const std::string& plugin,
                            const base::DictValue& snapshot) = 0;
    // A published source changed (<plugin-id>/<source>).
    virtual void OnSourceSnapshot(const std::string& source,
                                  const base::Value& data) {}
    virtual void OnNotify(const std::string& plugin,
                          const PluginNotification& notification) = 0;
    virtual void OnToast(const std::string& plugin,
                         const PluginToast& toast) = 0;
    // A T2 plugin exited on its own: restart_in says when it comes back, or
    // nullopt when it has failed for good.
    virtual void OnCrash(const std::string& plugin,
                         const ProcessPlugin::Crash& crash) = 0;
    // A T1 {open: <page>} handler ran.
    virtual void OnOpenControlPage(const std::string& plugin,
                                   const std::string& page) {}
  };

  struct Options {
    Options();
    Options(const Options&) = delete;
    Options(Options&&);
    Options& operator=(const Options&) = delete;
    Options& operator=(Options&&);
    ~Options();

    // The template for every T2 plugin (timeouts, backoff, launch path,
    // locale). Its capabilities are replaced by the registry's set and its
    // config by `config` below.
    ProcessPlugin::Options process;
    // User configuration per plugin id; declared defaults fill the rest.
    std::map<std::string, base::DictValue> config;
  };

  using ResultCallback = base::OnceCallback<void(JsonRpcResult)>;

  // `registry`, `delegate` and `wm` (nullable) must outlive the host.
  PluginHost(const PluginRegistry* registry,
             Delegate* delegate,
             WmCommandSink* wm,
             Options options);
  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;
  ~PluginHost() override;

  // Activates every registered plugin with onStartup, and loads every T1
  // plugin's trees (a T1 plugin costs no process).
  void Start();

  // Starts one plugin now. False for an unknown id or a T0 plugin.
  bool Activate(const std::string& plugin_id);

  // A command from any face ("views", "chrome", "cli", "keybinding",
  // "menu", "launcher"): <plugin-id>/<command>. Activates the plugin.
  void Invoke(const std::string& qualified_command,
              base::DictValue args,
              std::string_view source,
              ResultCallback callback);
  void SurfaceOpened(const std::string& plugin_id,
                     const std::string& surface,
                     const std::string& instance,
                     ResultCallback callback);
  void SurfaceClosed(const std::string& plugin_id,
                     const std::string& surface,
                     const std::string& instance,
                     ResultCallback callback);
  // A compositor or session event, to every plugin that declared it.
  void DeliverEvent(const std::string& name, const base::Value& data);

  // Shuts every T2 plugin down within its grace period, then runs `done`.
  void Shutdown(base::OnceClosure done);

  ProcessPlugin* process_plugin(std::string_view id);
  DeclarativePlugin* declarative_plugin(std::string_view id);
  const PermissionsBroker& broker() const { return broker_; }

  // TreeSink:
  void OnTree(const PluginManifest& plugin,
              const std::string& surface,
              base::DictValue tree) override;
  void OnSnapshot(const PluginManifest& plugin, base::DictValue data) override;
  void OnSourceSnapshot(const PluginManifest& plugin,
                        const std::string& source,
                        base::Value data) override;

  // PermissionsBroker::Client:
  void OnNotify(const PluginManifest& plugin,
                const PluginNotification& notification) override;
  void OnToast(const PluginManifest& plugin, const PluginToast& toast) override;

  // ProcessPlugin::Observer:
  void OnPluginCrashed(const ProcessPlugin& plugin,
                       const ProcessPlugin::Crash& crash) override;

  // DeclarativePlugin::Host:
  void OpenControlPage(const PluginManifest& plugin,
                       const std::string& page) override;
  void CallCommand(const PluginManifest& caller,
                   const std::string& command,
                   base::DictValue args,
                   ResultCallback callback) override;

 private:
  struct PluginState {
    PluginState();
    PluginState(PluginState&&);
    PluginState& operator=(PluginState&&);
    ~PluginState();

    std::optional<base::DictValue> snapshot;  // T2: nullopt until the first
    std::map<std::string, base::DictValue> trees;
  };

  PluginState& StateOf(const std::string& plugin_id);

  const raw_ptr<const PluginRegistry> registry_;
  const raw_ptr<Delegate> delegate_;
  Options options_;
  PermissionsBroker broker_;
  std::map<std::string, std::unique_ptr<ProcessPlugin>, std::less<>> processes_;
  std::map<std::string, std::unique_ptr<DeclarativePlugin>, std::less<>>
      declaratives_;
  std::map<std::string, PluginState> states_;
  std::map<std::string, base::Value> sources_;
  base::WeakPtrFactory<PluginHost> weak_factory_{this};
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_PLUGIN_HOST_H_
