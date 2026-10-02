// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/declarative_plugin.h"

#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "views_shell/plugins/permissions_broker.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {

namespace {

JsonRpcError Invalid(std::string message) {
  return JsonRpcError(kJsonRpcInvalidParams, std::move(message));
}

bool ArgMatches(const base::Value& value, const std::string& type) {
  if (type == "string") {
    return value.is_string();
  }
  if (type == "boolean") {
    return value.is_bool();
  }
  if (type == "number") {
    return value.is_int() || value.is_double();
  }
  return value.is_int();
}

}  // namespace

DeclarativePlugin::DeclarativePlugin(const PluginManifest& manifest,
                                     TreeSink* sink,
                                     PermissionsBroker* broker,
                                     Host* host)
    : manifest_(manifest), sink_(sink), broker_(broker), host_(host) {
  CHECK(manifest.tier == PluginTier::kDeclarative);
}

DeclarativePlugin::~DeclarativePlugin() = default;

std::vector<std::string> DeclarativePlugin::Load() {
  std::vector<std::string> problems;
  std::vector<std::pair<std::string, std::string>> files;  // (surface, ui)
  for (const PluginSurface& surface : manifest_->surfaces) {
    if (surface.ui) {
      files.emplace_back(surface.id, *surface.ui);
    }
  }
  for (const PluginQuickSettingsEntry& entry : manifest_->quick_settings) {
    if (entry.ui) {
      files.emplace_back(entry.id, *entry.ui);
    }
  }
  loaded_.clear();
  for (const auto& [surface, ui] : files) {
    std::string text;
    if (!base::ReadFileToString(manifest_->dir.AppendASCII(ui), &text)) {
      problems.push_back(base::StrCat({surface, ": cannot read ", ui}));
      continue;
    }
    std::optional<base::Value> tree =
        base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    if (!tree || !tree->is_dict()) {
      problems.push_back(
          base::StrCat({surface, ": ", ui, " is not a JSON object"}));
      continue;
    }
    std::vector<std::string> references;
    CollectCommandReferences(*tree, &references);
    std::string undeclared;
    for (const std::string& reference : references) {
      const bool local = reference.find('/') == std::string::npos;
      if ((local || reference.starts_with(manifest_->id + "/")) &&
          !manifest_->FindCommand(reference)) {
        undeclared = reference;
        break;
      }
      if (!local && !PermissionsBroker::AllowsCall(*manifest_, reference)) {
        undeclared = reference;
        break;
      }
    }
    if (!undeclared.empty()) {
      problems.push_back(base::StrCat(
          {surface, ": ", ui, " names ", undeclared,
           ", which the manifest neither declares nor may call"}));
      continue;
    }
    loaded_.push_back(surface);
    sink_->OnTree(*manifest_, surface, std::move(tree->GetDict()));
  }
  for (const std::string& problem : problems) {
    LOG(WARNING) << "plugin " << manifest_->id << ": " << problem;
  }
  return problems;
}

void DeclarativePlugin::Invoke(std::string_view command,
                               base::DictValue args,
                               ResultCallback callback) {
  const PluginCommand* declared = manifest_->FindCommand(command);
  if (!declared) {
    std::move(callback).Run(base::unexpected(Invalid(base::StrCat(
        {"command ", command, " is not declared by ", manifest_->id}))));
    return;
  }
  for (const PluginCommandArg& arg : declared->args) {
    const base::Value* value = args.Find(arg.name);
    if ((!value && arg.required) || (value && !ArgMatches(*value, arg.type))) {
      std::move(callback).Run(base::unexpected(Invalid(base::StrCat(
          {"argument ", arg.name, " of ", declared->id, " must be a ",
           arg.type}))));
      return;
    }
  }
  const base::DictValue* handler = declared->handler.GetIfDict();
  if (!handler) {
    // A verb-only command acts on a surface; views-shell's surface host
    // answers it, never the plugin.
    std::move(callback).Run(base::unexpected(Invalid(base::StrCat(
        {"command ", declared->id, " has no handler"}))));
    return;
  }
  // Static handler args, then the invocation's args over them.
  base::DictValue merged;
  if (const base::DictValue* fixed = handler->FindDict("args")) {
    merged = fixed->Clone();
  }
  merged.Merge(args.Clone());

  if (const std::string* capability = handler->FindString("compositor")) {
    broker_->HandleRequest(*manifest_, "compositor/command",
                           base::DictValue()
                               .Set("capability", *capability)
                               .Set("args", std::move(merged)),
                           std::move(callback));
    return;
  }
  if (const std::string* program = handler->FindString("exec")) {
    base::ListValue argv;
    if (const base::ListValue* fixed = handler->FindList("args")) {
      argv = fixed->Clone();
    }
    broker_->HandleRequest(
        *manifest_, "exec",
        base::DictValue().Set("program", *program).Set("args", std::move(argv)),
        base::BindOnce(
            [](ResultCallback callback, JsonRpcResult result) {
              // A T1 command answers like result none; a failed program is
              // an error.
              if (result.has_value()) {
                const std::optional<int> code =
                    result->GetDict().FindInt("exitCode");
                if (code.value_or(1) != 0) {
                  std::move(callback).Run(base::unexpected(JsonRpcError(
                      kJsonRpcRequestFailed, "the handler's program failed",
                      std::move(result).value())));
                  return;
                }
                std::move(callback).Run(base::Value(base::DictValue()));
                return;
              }
              std::move(callback).Run(std::move(result));
            },
            std::move(callback)));
    return;
  }
  if (const std::string* page = handler->FindString("open")) {
    host_->OpenControlPage(*manifest_, *page);
    std::move(callback).Run(base::Value(base::DictValue()));
    return;
  }
  if (const std::string* target = handler->FindString("call")) {
    const std::string qualified = manifest_->Qualify(*target);
    if (!PermissionsBroker::AllowsCall(*manifest_, qualified)) {
      std::move(callback).Run(base::unexpected(JsonRpcError(
          kJsonRpcPermissionNotDeclared, "permission not declared",
          base::Value(base::DictValue().Set(
              "permission", base::StrCat({"call:", qualified}))))));
      return;
    }
    host_->CallCommand(*manifest_, qualified, std::move(merged),
                       std::move(callback));
    return;
  }
  std::move(callback).Run(base::unexpected(
      Invalid(base::StrCat({"command ", declared->id,
                            " has a handler of no known kind"}))));
}

}  // namespace views_shell
