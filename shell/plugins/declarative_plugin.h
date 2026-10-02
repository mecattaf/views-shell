// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A T1 plugin: no code. Its surfaces and quick-settings entries name ui tree
// files, which are read once and handed to the TreeSink, and its commands
// carry declarative handlers that views-shell runs on the plugin's behalf:
//
//   {compositor: <capability>, args}  a typed compositor command, through the
//                                     PermissionsBroker (compositor:<cap>)
//   {open: <page>}                    a views-shell control page, opened in
//                                     Chrome by the host
//   {call: <command>, args}           another command, local or qualified
//                                     (call: permission for another plugin's)
//   {exec: <program>, args}           a program, through the broker
//                                     (exec:<program>)
//
// The handler is answered like a T2 command/invoke: {} for result none.

#ifndef VIEWS_SHELL_PLUGINS_DECLARATIVE_PLUGIN_H_
#define VIEWS_SHELL_PLUGINS_DECLARATIVE_PLUGIN_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/values.h"
#include "views_shell/plugins/json_rpc.h"

namespace views_shell {

class PermissionsBroker;
class TreeSink;
struct PluginManifest;

class DeclarativePlugin {
 public:
  using ResultCallback = base::OnceCallback<void(JsonRpcResult)>;

  // What the open and call handlers need from the host.
  class Host {
   public:
    virtual ~Host() = default;
    virtual void OpenControlPage(const PluginManifest& plugin,
                                 const std::string& page) = 0;
    // Invokes `command` (qualified) as `caller`; permission already checked.
    virtual void CallCommand(const PluginManifest& caller,
                             const std::string& command,
                             base::DictValue args,
                             ResultCallback callback) = 0;
  };

  // Everything must outlive this object.
  DeclarativePlugin(const PluginManifest& manifest,
                    TreeSink* sink,
                    PermissionsBroker* broker,
                    Host* host);
  DeclarativePlugin(const DeclarativePlugin&) = delete;
  DeclarativePlugin& operator=(const DeclarativePlugin&) = delete;
  ~DeclarativePlugin();

  // Reads every ui file the manifest names and hands each tree to the sink
  // (surfaces first, then quick-settings entries). A file that cannot be read,
  // is not a JSON object, or names a local command the manifest does not
  // declare is not delivered; the returned list says why, one line each.
  std::vector<std::string> Load();

  // Runs a declared command's handler. Arguments are checked against the
  // declared args first (-32602).
  void Invoke(std::string_view command,
              base::DictValue args,
              ResultCallback callback);

  const PluginManifest& manifest() const { return *manifest_; }
  // Surfaces and entries whose tree was delivered.
  const std::vector<std::string>& loaded() const { return loaded_; }

 private:
  const raw_ref<const PluginManifest> manifest_;
  const raw_ptr<TreeSink> sink_;
  const raw_ptr<PermissionsBroker> broker_;
  const raw_ptr<Host> host_;
  std::vector<std::string> loaded_;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_PLUGINS_DECLARATIVE_PLUGIN_H_
