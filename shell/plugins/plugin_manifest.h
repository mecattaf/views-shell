// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// views-shell-plugin.json, read without running plugin code (rule R11,
// docs/architecture.md §6). Chromium 154 has no JSON Schema validator, so every
// shape rule of schemas/views-shell-plugin.schema.json is written out by hand
// in plugin_manifest.cc, keyword by keyword in the schema's own order, and
// reports the message the Python `jsonschema` package prints for the same
// failure. Problems are sorted by instance path the way tools/validate.py
// sorts them, so the first problem is the reason tools/plugin-registry.py
// prints after REJECT (object reprs list keys sorted on both sides). The
// cross-file checks of validate.py's manifest_problems() follow: named files
// exist, local command references are declared, pages are declared, and
// cross-plugin calls are covered by a call: permission.

#ifndef VIEWS_SHELL_PLUGINS_PLUGIN_MANIFEST_H_
#define VIEWS_SHELL_PLUGINS_PLUGIN_MANIFEST_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "views_shell/wm/compositor_adapter.h"

namespace views_shell {

inline constexpr char kPluginManifestFileName[] = "views-shell-plugin.json";

// One step of a JSON instance path: an object key or an array index.
using ManifestPathPart = std::variant<std::string, size_t>;

struct ManifestProblem {
  ManifestProblem();
  ManifestProblem(std::vector<ManifestPathPart> path, std::string message);
  ManifestProblem(const ManifestProblem&);
  ManifestProblem(ManifestProblem&&);
  ManifestProblem& operator=(const ManifestProblem&);
  ManifestProblem& operator=(ManifestProblem&&);
  ~ManifestProblem();

  // "<a/0/b>: <message>", or "(root): <message>" for the top level: the
  // reason tools/plugin-registry.py prints after REJECT <file>.
  std::string ToReason() const;

  std::vector<ManifestPathPart> path;
  std::string message;
};

// Every schema problem of one parsed manifest, stably sorted by instance path.
// Empty means the manifest is schema-valid.
std::vector<ManifestProblem> CheckManifestSchema(const base::Value& manifest);

// validate.py's cross-file checks for a schema-valid manifest in `dir`, each
// as validate.py words it after the file name: "names missing file <f>",
// "references undeclared command <c>", "entry <e> opens undeclared page <p>",
// "calls <c> without a call: permission".
std::vector<std::string> CheckManifestFiles(const base::DictValue& manifest,
                                            const base::FilePath& dir);

// Python's repr() of a JSON value as jsonschema prints it in messages (dict
// keys sorted: base::DictValue keeps no insertion order).
std::string PyRepr(const base::Value& value);

// Every string under a "command" or "call" key, anywhere (validate.py's
// commands_in()).
void CollectCommandReferences(const base::Value& node,
                              std::vector<std::string>* out);

// A command reference is covered when it is local, names this plugin, or a
// call:<plugin>/* or call:<plugin>/<command> permission covers it.
bool CommandReferenceCovered(std::string_view plugin_id,
                             const base::flat_set<std::string>& permissions,
                             std::string_view command);

enum class PluginTier { kBuiltin, kDeclarative, kProcess };
std::string_view PluginTierName(PluginTier tier);

struct PluginCommandArg {
  PluginCommandArg();
  PluginCommandArg(const PluginCommandArg&);
  PluginCommandArg(PluginCommandArg&&);
  PluginCommandArg& operator=(const PluginCommandArg&);
  PluginCommandArg& operator=(PluginCommandArg&&);
  ~PluginCommandArg();

  std::string name;
  std::string type;  // string, integer, number, boolean
  bool required = false;
};

struct PluginCommand {
  PluginCommand();
  PluginCommand(PluginCommand&&);
  PluginCommand& operator=(PluginCommand&&);
  ~PluginCommand();

  std::string id;  // local id
  std::string title;
  std::vector<PluginCommandArg> args;
  std::string result = "none";  // none, text, json
  std::optional<std::string> surface;
  std::optional<std::string> verb;  // a surface verb: views-shell's own
  bool confirm = false;
  base::Value handler;  // T1 only; NONE otherwise
};

struct PluginSurface {
  PluginSurface();
  PluginSurface(const PluginSurface&);
  PluginSurface(PluginSurface&&);
  PluginSurface& operator=(const PluginSurface&);
  PluginSurface& operator=(PluginSurface&&);
  ~PluginSurface();

  std::string id;
  std::string kind;
  std::optional<std::string> ui;
};

struct PluginQuickSettingsEntry {
  PluginQuickSettingsEntry();
  PluginQuickSettingsEntry(const PluginQuickSettingsEntry&);
  PluginQuickSettingsEntry(PluginQuickSettingsEntry&&);
  PluginQuickSettingsEntry& operator=(const PluginQuickSettingsEntry&);
  PluginQuickSettingsEntry& operator=(PluginQuickSettingsEntry&&);
  ~PluginQuickSettingsEntry();

  std::string id;
  std::string slot;
  std::optional<std::string> ui;
  std::optional<std::string> state;  // <plugin-id>/<source>
};

// A validated manifest. Move-only: the registry owns it, instances point at it.
struct PluginManifest {
  PluginManifest();
  PluginManifest(const PluginManifest&) = delete;
  PluginManifest(PluginManifest&&);
  PluginManifest& operator=(const PluginManifest&) = delete;
  PluginManifest& operator=(PluginManifest&&);
  ~PluginManifest();

  // The command with this local id, or with this plugin's qualified id.
  const PluginCommand* FindCommand(std::string_view command) const;
  bool HasPermission(std::string_view permission) const;
  // Surfaces whose tree the plugin sends: every surface but kinds service and
  // picker-provider, then every quick-settings entry (by entry id).
  std::vector<std::string> TreeSurfaces() const;
  // "<id>/<command>" for a local reference, unchanged when qualified.
  std::string Qualify(std::string_view reference) const;

  base::FilePath dir;    // the plugin directory (absolute)
  base::DictValue raw;   // the manifest as parsed
  std::string id;
  std::string name;
  std::string version;
  PluginTier tier = PluginTier::kDeclarative;
  std::optional<int> protocol;     // engines.protocol
  std::string exec;                // runtime.exec (relative to dir), T2
  std::vector<std::string> exec_args;
  std::string target;              // runtime.target, T0
  base::flat_set<std::string> permissions;
  CapabilitySet required_capabilities;
  CapabilitySet optional_capabilities;
  std::vector<std::string> dependencies;
  std::vector<PluginCommand> commands;
  std::vector<PluginSurface> surfaces;
  std::vector<PluginQuickSettingsEntry> quick_settings;
  std::vector<std::string> events;
  std::vector<std::string> sources;  // local source ids
  base::DictValue config_defaults;   // every property that declares a default
  std::optional<std::string> cli_name;
  std::vector<std::string> keybinding_keys;  // resolved keys, manifest order
};

// Where a running plugin's output goes: the trees of its surfaces (from
// surface/setTree, or a T1 ui file), its full snapshots, and the sources it
// publishes. Declared beside the manifest because every plugin instance,
// declarative or process, writes to one. PluginHost implements it and holds
// the latest tree and snapshot per surface; the renderer (the ui_tree seam)
// binds to PluginHost::Delegate, which carries both together.
class TreeSink {
 public:
  virtual ~TreeSink() = default;
  // `surface` is a surface id or a quick-settings entry id; `tree` is a ui
  // tree (schemas/ui-tree.schema.json), whole.
  virtual void OnTree(const PluginManifest& plugin,
                      const std::string& surface,
                      base::DictValue tree) = 0;
  // The plugin's full snapshot; it replaces the previous one whole.
  virtual void OnSnapshot(const PluginManifest& plugin,
                          base::DictValue data) = 0;
  // A published source, qualified <plugin-id>/<source>, always whole.
  virtual void OnSourceSnapshot(const PluginManifest& plugin,
                                const std::string& source,
                                base::Value data) = 0;
};

// Parses a manifest value that CheckManifestSchema() accepted.
PluginManifest BuildPluginManifest(base::DictValue manifest,
                                   const base::FilePath& dir);

// Reads <dir>/views-shell-plugin.json (or a manifest file path), validates it
// (schema, then files) and builds it. The error is the REJECT reason.
base::expected<PluginManifest, std::string> LoadPluginManifest(
    const base::FilePath& dir_or_manifest);

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_PLUGIN_MANIFEST_H_
