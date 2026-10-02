// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The registry view: what views-shell registers from one manifest without
// running plugin code. The contract is tools/plugin-registry.py's docstring
// (field by field, defaults, inferred activation, qualified command
// references), and the committed views are tools/fixtures/registry/<id>.json.
// WriteRegistryJson() prints exactly what that tool prints (Python's
// json.dumps(indent=2, sort_keys=True, ensure_ascii=False) plus a newline), so
// the C++ view and the Python view compare byte for byte.

#ifndef VIEWS_SHELL_PLUGINS_REGISTRY_VIEW_H_
#define VIEWS_SHELL_PLUGINS_REGISTRY_VIEW_H_

#include <string>

#include "base/values.h"

namespace views_shell {

struct PluginManifest;

// The canonical registry view of a validated manifest.
base::DictValue BuildRegistryView(const PluginManifest& manifest);

// json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n".
std::string WriteRegistryJson(const base::Value& value);

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_REGISTRY_VIEW_H_
