// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The plugin registry (docs/architecture.md §6): discovers plugin directories
// under a list of roots, validates each manifest with the same checks as
// tools/validate.py (plugin_manifest.h), infers activation from contributions
// (registry_view.h), and registers what passes. A plugin whose required
// compositor capability the running adapter lacks is not registered (rule
// R14). Conflicts between registered plugins (a duplicate id, the same
// keybinding, the same CLI name, a dependency nobody provides) are recorded,
// never resolved silently.

#ifndef VIEWS_SHELL_PLUGINS_PLUGIN_REGISTRY_H_
#define VIEWS_SHELL_PLUGINS_PLUGIN_REGISTRY_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "views_shell/plugins/plugin_manifest.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell {

struct RegistryRejection {
  enum class Kind { kInvalid, kMissingCapability, kDuplicateId };

  RegistryRejection();
  RegistryRejection(const RegistryRejection&);
  RegistryRejection(RegistryRejection&&);
  RegistryRejection& operator=(const RegistryRejection&);
  RegistryRejection& operator=(RegistryRejection&&);
  ~RegistryRejection();

  Kind kind = Kind::kInvalid;
  base::FilePath dir;
  std::string plugin_id;  // empty when the manifest did not get that far
  std::string reason;
};

struct RegistryConflict {
  enum class Kind { kKeybinding, kCliName, kMissingDependency };

  RegistryConflict();
  RegistryConflict(const RegistryConflict&);
  RegistryConflict(RegistryConflict&&);
  RegistryConflict& operator=(const RegistryConflict&);
  RegistryConflict& operator=(RegistryConflict&&);
  ~RegistryConflict();

  Kind kind = Kind::kKeybinding;
  std::string key;                   // the key, CLI name or missing plugin id
  std::vector<std::string> plugins;  // the plugins involved, registry order
};

class PluginRegistry {
 public:
  // `capabilities` is the running adapter's set (CompositorAdapter::
  // capabilities()); the gate for rule R14.
  explicit PluginRegistry(CapabilitySet capabilities);
  PluginRegistry(const PluginRegistry&) = delete;
  PluginRegistry& operator=(const PluginRegistry&) = delete;
  ~PluginRegistry();

  // Each root is either a plugin directory (it holds views-shell-plugin.json)
  // or a directory of plugin directories, read in name order. Earlier roots
  // win a duplicate id. Recomputes conflicts.
  void Discover(const std::vector<base::FilePath>& roots);

  // Registers one plugin directory. False when it was rejected.
  bool AddPluginDirectory(const base::FilePath& dir);

  const PluginManifest* Find(std::string_view id) const;
  // The registry view (tools/fixtures/registry/<id>.json shape).
  const base::DictValue* ViewOf(std::string_view id) const;
  // Registered plugins in id order.
  std::vector<const PluginManifest*> plugins() const;
  // The plugin's inferred activation events.
  std::vector<std::string> ActivationOf(std::string_view id) const;
  bool StartsAtStartup(std::string_view id) const;

  const CapabilitySet& capabilities() const { return capabilities_; }
  const std::vector<RegistryRejection>& rejected() const { return rejected_; }
  const std::vector<RegistryConflict>& conflicts() const { return conflicts_; }

 private:
  struct Entry {
    Entry();
    Entry(Entry&&);
    Entry& operator=(Entry&&);
    ~Entry();

    std::unique_ptr<PluginManifest> manifest;
    base::DictValue view;
  };

  void RecomputeConflicts();

  const CapabilitySet capabilities_;
  std::map<std::string, Entry, std::less<>> entries_;
  std::vector<std::string> order_;  // registration order
  std::vector<RegistryRejection> rejected_;
  std::vector<RegistryConflict> conflicts_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_PLUGIN_REGISTRY_H_
