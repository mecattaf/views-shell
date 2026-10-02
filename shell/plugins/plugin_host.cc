// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_host.h"

#include <algorithm>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "views_shell/plugins/plugin_registry.h"

namespace views_shell {

PluginHost::Options::Options() = default;
PluginHost::Options::Options(Options&&) = default;
PluginHost::Options& PluginHost::Options::operator=(Options&&) = default;
PluginHost::Options::~Options() = default;

PluginHost::PluginState::PluginState() = default;
PluginHost::PluginState::PluginState(PluginState&&) = default;
PluginHost::PluginState& PluginHost::PluginState::operator=(PluginState&&) =
    default;
PluginHost::PluginState::~PluginState() = default;

PluginHost::PluginHost(const PluginRegistry* registry,
                       Delegate* delegate,
                       WmCommandSink* wm,
                       Options options)
    : registry_(registry),
      delegate_(delegate),
      options_(std::move(options)),
      broker_(this, wm) {}

PluginHost::~PluginHost() {
  // Instances point at the broker and at this sink; end them first.
  processes_.clear();
  declaratives_.clear();
}

PluginHost::PluginState& PluginHost::StateOf(const std::string& plugin_id) {
  return states_[plugin_id];
}

void PluginHost::Start() {
  for (const PluginManifest* plugin : registry_->plugins()) {
    if (plugin->tier == PluginTier::kDeclarative ||
        registry_->StartsAtStartup(plugin->id)) {
      Activate(plugin->id);
    }
  }
}

bool PluginHost::Activate(const std::string& plugin_id) {
  const PluginManifest* plugin = registry_->Find(plugin_id);
  if (!plugin || plugin->tier == PluginTier::kBuiltin) {
    return false;
  }
  if (plugin->tier == PluginTier::kDeclarative) {
    if (!declaratives_.contains(plugin_id)) {
      auto instance =
          std::make_unique<DeclarativePlugin>(*plugin, this, &broker_, this);
      DeclarativePlugin* raw = instance.get();
      declaratives_.emplace(plugin_id, std::move(instance));
      raw->Load();
    }
    return true;
  }
  auto it = processes_.find(plugin_id);
  if (it == processes_.end()) {
    ProcessPlugin::Options process;
    process.launch_path = options_.process.launch_path;
    process.startup_window = options_.process.startup_window;
    process.request_timeout = options_.process.request_timeout;
    process.shutdown_grace = options_.process.shutdown_grace;
    process.initial_backoff = options_.process.initial_backoff;
    process.max_backoff = options_.process.max_backoff;
    process.max_restarts = options_.process.max_restarts;
    process.stable_after = options_.process.stable_after;
    process.shell_version = options_.process.shell_version;
    process.locale = options_.process.locale;
    process.capabilities = registry_->capabilities();
    if (auto config = options_.config.find(plugin_id);
        config != options_.config.end()) {
      process.config = config->second.Clone();
    }
    it = processes_
             .emplace(plugin_id, std::make_unique<ProcessPlugin>(
                                     *plugin, std::move(process), this,
                                     &broker_, this))
             .first;
  }
  ProcessPlugin::State state = it->second->state();
  if (state == ProcessPlugin::State::kStopped ||
      state == ProcessPlugin::State::kExited) {
    it->second->Start();
  }
  return true;
}

ProcessPlugin* PluginHost::process_plugin(std::string_view id) {
  auto it = processes_.find(id);
  return it == processes_.end() ? nullptr : it->second.get();
}

DeclarativePlugin* PluginHost::declarative_plugin(std::string_view id) {
  auto it = declaratives_.find(id);
  return it == declaratives_.end() ? nullptr : it->second.get();
}

void PluginHost::Invoke(const std::string& qualified_command,
                        base::DictValue args,
                        std::string_view source,
                        ResultCallback callback) {
  const size_t slash = qualified_command.find('/');
  const std::string plugin_id = qualified_command.substr(0, slash);
  const PluginManifest* plugin =
      slash == std::string::npos ? nullptr : registry_->Find(plugin_id);
  const PluginCommand* command =
      plugin ? plugin->FindCommand(qualified_command) : nullptr;
  if (!command) {
    std::move(callback).Run(base::unexpected(JsonRpcError(
        kJsonRpcMethodNotFound,
        base::StrCat({"no registered command ", qualified_command}))));
    return;
  }
  if (command->verb) {
    std::move(callback).Run(base::unexpected(JsonRpcError(
        kJsonRpcInvalidParams,
        base::StrCat({qualified_command, " is a surface verb; the surface "
                                         "host handles it"}))));
    return;
  }
  if (plugin->tier == PluginTier::kBuiltin) {
    std::move(callback).Run(base::unexpected(JsonRpcError(
        kJsonRpcMethodNotFound,
        base::StrCat({qualified_command,
                      " belongs to a builtin plugin, which its own C++ "
                      "serves"}))));
    return;
  }
  Activate(plugin_id);
  if (plugin->tier == PluginTier::kDeclarative) {
    declaratives_.at(plugin_id)->Invoke(qualified_command, std::move(args),
                                        std::move(callback));
    return;
  }
  processes_.find(plugin_id)
      ->second->Invoke(qualified_command, std::move(args), source,
                       std::nullopt, std::move(callback));
}

void PluginHost::SurfaceOpened(const std::string& plugin_id,
                               const std::string& surface,
                               const std::string& instance,
                               ResultCallback callback) {
  const PluginManifest* plugin = registry_->Find(plugin_id);
  if (plugin && plugin->tier == PluginTier::kProcess) {
    Activate(plugin_id);
    processes_.find(plugin_id)->second->SurfaceOpened(surface, instance,
                                                      std::move(callback));
    return;
  }
  std::move(callback).Run(base::Value(base::DictValue()));
}

void PluginHost::SurfaceClosed(const std::string& plugin_id,
                               const std::string& surface,
                               const std::string& instance,
                               ResultCallback callback) {
  ProcessPlugin* process = process_plugin(plugin_id);
  if (process) {
    process->SurfaceClosed(surface, instance, std::move(callback));
    return;
  }
  std::move(callback).Run(base::Value(base::DictValue()));
}

void PluginHost::DeliverEvent(const std::string& name,
                              const base::Value& data) {
  for (const PluginManifest* plugin : registry_->plugins()) {
    if (plugin->tier != PluginTier::kProcess ||
        std::ranges::find(plugin->events, name) == plugin->events.end()) {
      continue;
    }
    Activate(plugin->id);
    // A plugin that is still starting misses this one; the next event, and
    // its own startup snapshot, carry the state.
    processes_.find(plugin->id)->second->DeliverEvent(name, data.Clone());
  }
}

void PluginHost::Shutdown(base::OnceClosure done) {
  std::vector<ProcessPlugin*> running;
  for (auto& [id, process] : processes_) {
    running.push_back(process.get());
  }
  base::RepeatingClosure barrier =
      base::BarrierClosure(running.size(), std::move(done));
  for (ProcessPlugin* process : running) {
    process->Shutdown(base::BindOnce(
        [](base::RepeatingClosure one_done, std::string id, int exit_code) {
          VLOG(1) << "plugin " << id << " shut down with " << exit_code;
          one_done.Run();
        },
        barrier, process->manifest().id));
  }
}

void PluginHost::OnTree(const PluginManifest& plugin,
                        const std::string& surface,
                        base::DictValue tree) {
  PluginState& state = StateOf(plugin.id);
  auto [it, unused] = state.trees.insert_or_assign(surface, std::move(tree));
  if (plugin.tier == PluginTier::kDeclarative) {
    delegate_->OnTree(plugin.id, surface, it->second, base::DictValue());
  } else if (state.snapshot) {
    delegate_->OnTree(plugin.id, surface, it->second, *state.snapshot);
  }
  // Otherwise the first snapshot delivers it.
}

void PluginHost::OnSnapshot(const PluginManifest& plugin,
                            base::DictValue data) {
  PluginState& state = StateOf(plugin.id);
  const bool first = !state.snapshot;
  state.snapshot = std::move(data);
  if (first) {
    for (const auto& [surface, tree] : state.trees) {
      delegate_->OnTree(plugin.id, surface, tree, *state.snapshot);
    }
  }
  delegate_->OnSnapshot(plugin.id, *state.snapshot);
}

void PluginHost::OnSourceSnapshot(const PluginManifest& plugin,
                                  const std::string& source,
                                  base::Value data) {
  sources_.insert_or_assign(source, data.Clone());
  delegate_->OnSourceSnapshot(source, data);
  for (auto& [id, process] : processes_) {
    if (id != plugin.id) {
      process->DeliverSourceChanged(source, data.Clone());
    }
  }
}

void PluginHost::OnNotify(const PluginManifest& plugin,
                          const PluginNotification& notification) {
  delegate_->OnNotify(plugin.id, notification);
}

void PluginHost::OnToast(const PluginManifest& plugin,
                         const PluginToast& toast) {
  delegate_->OnToast(plugin.id, toast);
}

void PluginHost::OnPluginCrashed(const ProcessPlugin& plugin,
                                 const ProcessPlugin::Crash& crash) {
  // The trees stay (the surface host badges them); the snapshot is resent by
  // the restarted process.
  StateOf(plugin.manifest().id).snapshot.reset();
  delegate_->OnCrash(plugin.manifest().id, crash);
}

void PluginHost::OpenControlPage(const PluginManifest& plugin,
                                 const std::string& page) {
  delegate_->OnOpenControlPage(plugin.id, page);
}

void PluginHost::CallCommand(const PluginManifest& caller,
                             const std::string& command,
                             base::DictValue args,
                             ResultCallback callback) {
  VLOG(1) << "plugin " << caller.id << " calls " << command;
  Invoke(command, std::move(args), "views", std::move(callback));
}

}  // namespace views_shell
