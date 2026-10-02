// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/plugin_registry.h"

#include <algorithm>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "views_shell/plugins/registry_view.h"

namespace views_shell {

RegistryRejection::RegistryRejection() = default;
RegistryRejection::RegistryRejection(const RegistryRejection&) = default;
RegistryRejection::RegistryRejection(RegistryRejection&&) = default;
RegistryRejection& RegistryRejection::operator=(const RegistryRejection&) =
    default;
RegistryRejection& RegistryRejection::operator=(RegistryRejection&&) = default;
RegistryRejection::~RegistryRejection() = default;

RegistryConflict::RegistryConflict() = default;
RegistryConflict::RegistryConflict(const RegistryConflict&) = default;
RegistryConflict::RegistryConflict(RegistryConflict&&) = default;
RegistryConflict& RegistryConflict::operator=(const RegistryConflict&) =
    default;
RegistryConflict& RegistryConflict::operator=(RegistryConflict&&) = default;
RegistryConflict::~RegistryConflict() = default;

PluginRegistry::Entry::Entry() = default;
PluginRegistry::Entry::Entry(Entry&&) = default;
PluginRegistry::Entry& PluginRegistry::Entry::operator=(Entry&&) = default;
PluginRegistry::Entry::~Entry() = default;

PluginRegistry::PluginRegistry(CapabilitySet capabilities)
    : capabilities_(std::move(capabilities)) {}

PluginRegistry::~PluginRegistry() = default;

void PluginRegistry::Discover(const std::vector<base::FilePath>& roots) {
  for (const base::FilePath& root : roots) {
    if (base::PathExists(root.AppendASCII(kPluginManifestFileName))) {
      AddPluginDirectory(root);
      continue;
    }
    std::vector<base::FilePath> dirs;
    base::FileEnumerator walk(root, /*recursive=*/false,
                              base::FileEnumerator::DIRECTORIES);
    for (base::FilePath dir = walk.Next(); !dir.empty(); dir = walk.Next()) {
      if (base::PathExists(dir.AppendASCII(kPluginManifestFileName))) {
        dirs.push_back(dir);
      }
    }
    std::sort(dirs.begin(), dirs.end());
    for (const base::FilePath& dir : dirs) {
      AddPluginDirectory(dir);
    }
  }
  RecomputeConflicts();
}

bool PluginRegistry::AddPluginDirectory(const base::FilePath& dir) {
  base::expected<PluginManifest, std::string> loaded = LoadPluginManifest(dir);
  RegistryRejection rejection;
  rejection.dir = dir;
  if (!loaded.has_value()) {
    rejection.reason = loaded.error();
    LOG(WARNING) << "plugin registry: REJECT " << dir.value() << " "
                 << rejection.reason;
    rejected_.push_back(std::move(rejection));
    return false;
  }
  auto manifest = std::make_unique<PluginManifest>(std::move(loaded).value());
  rejection.plugin_id = manifest->id;
  if (const auto it = entries_.find(manifest->id); it != entries_.end()) {
    rejection.kind = RegistryRejection::Kind::kDuplicateId;
    rejection.reason = base::StrCat({"duplicate id ", manifest->id,
                                     ", already registered from ",
                                     it->second.manifest->dir.value()});
  } else {
    std::vector<std::string> missing;
    for (const Capability& capability : manifest->required_capabilities) {
      if (!capabilities_.contains(capability)) {
        missing.push_back(capability);
      }
    }
    if (!missing.empty()) {
      rejection.kind = RegistryRejection::Kind::kMissingCapability;
      rejection.reason =
          base::StrCat({"missing required capability ",
                        base::JoinString(missing, ", "), " (rule R14)"});
    }
  }
  if (!rejection.reason.empty()) {
    LOG(WARNING) << "plugin registry: not registered " << manifest->id << ": "
                 << rejection.reason;
    rejected_.push_back(std::move(rejection));
    return false;
  }
  Entry entry;
  entry.view = BuildRegistryView(*manifest);
  entry.manifest = std::move(manifest);
  const std::string id = entry.manifest->id;
  order_.push_back(id);
  entries_.emplace(id, std::move(entry));
  return true;
}

void PluginRegistry::RecomputeConflicts() {
  conflicts_.clear();
  std::map<std::string, std::vector<std::string>> keys;
  std::map<std::string, std::vector<std::string>> cli_names;
  for (const std::string& id : order_) {
    const PluginManifest& m = *entries_.at(id).manifest;
    for (const std::string& key : m.keybinding_keys) {
      std::vector<std::string>& owners = keys[key];
      if (std::ranges::find(owners, id) == owners.end()) {
        owners.push_back(id);
      }
    }
    if (m.cli_name) {
      cli_names[*m.cli_name].push_back(id);
    }
    for (const std::string& dependency : m.dependencies) {
      if (!entries_.contains(dependency)) {
        RegistryConflict conflict;
        conflict.kind = RegistryConflict::Kind::kMissingDependency;
        conflict.key = dependency;
        conflict.plugins = {id};
        conflicts_.push_back(std::move(conflict));
      }
    }
  }
  for (auto [table, kind] :
       {std::make_pair(&keys, RegistryConflict::Kind::kKeybinding),
        std::make_pair(&cli_names, RegistryConflict::Kind::kCliName)}) {
    for (auto& [key, owners] : *table) {
      if (owners.size() > 1) {
        RegistryConflict conflict;
        conflict.kind = kind;
        conflict.key = key;
        conflict.plugins = owners;
        conflicts_.push_back(std::move(conflict));
      }
    }
  }
}

const PluginManifest* PluginRegistry::Find(std::string_view id) const {
  const auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : it->second.manifest.get();
}

const base::DictValue* PluginRegistry::ViewOf(std::string_view id) const {
  const auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : &it->second.view;
}

std::vector<const PluginManifest*> PluginRegistry::plugins() const {
  std::vector<const PluginManifest*> out;
  for (const auto& [id, entry] : entries_) {
    out.push_back(entry.manifest.get());
  }
  return out;
}

std::vector<std::string> PluginRegistry::ActivationOf(
    std::string_view id) const {
  std::vector<std::string> out;
  const base::DictValue* view = ViewOf(id);
  const base::ListValue* activation =
      view ? view->FindList("activation") : nullptr;
  if (activation) {
    for (const base::Value& event : *activation) {
      out.push_back(event.GetString());
    }
  }
  return out;
}

bool PluginRegistry::StartsAtStartup(std::string_view id) const {
  const std::vector<std::string> activation = ActivationOf(id);
  return std::ranges::find(activation, "onStartup") != activation.end();
}

}  // namespace views_shell
