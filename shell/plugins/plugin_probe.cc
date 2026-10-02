// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// plugin_probe: the plugin host's run gate. A console program (no Wayland, no
// Views) that registers one plugin directory with the real registry, runs it
// with the real PluginHost and PermissionsBroker, and prints what views-shell
// would hand the renderer.
//
//   plugin_probe --plugin <dir> [--invoke <command> [--args <json>]]
//                [--seconds <n>] [--capabilities <a,b,...>]
//
// stdout, one line each:
//   REGISTRY <id> then the registry view (tools/plugin-registry.py's bytes)
//   surface/setTree <surface> <tree>      every tree the host delivers
//   snapshot <data>                       every snapshot
//   source/snapshot <source> <data>
//   notify <params> / toast <params>      what the broker passed on
//   crash <reason> exit <n> restart_in <ms|never>
//   invoke <command> <args> -> result <answer> | error <code> <message>
//   plugin alive: true|false              after --seconds (default 5)
//   launch path: scope|plain
//   shutdown exit <status>
// Exit status 0 when the plugin registered, the invoke (if any) got a result,
// and a T2 plugin was alive at the end; 1 otherwise; 2 for bad arguments.
// --capabilities is the compositor capability set the registry gates on
// (rule R14); without it the set is empty.

#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "base/values.h"
#include "views_shell/plugins/plugin_host.h"
#include "views_shell/plugins/plugin_registry.h"
#include "views_shell/plugins/registry_view.h"

namespace views_shell {
namespace {

constexpr char kUsage[] =
    "usage: plugin_probe --plugin <dir> [--invoke <command> [--args <json>]]\n"
    "                    [--seconds <n>] [--capabilities <a,b,...>]\n";

void Print(std::string_view line) {
  base::WriteFileDescriptor(STDOUT_FILENO, base::StrCat({line, "\n"}));
}

std::string Json(const base::Value& value) {
  std::string out;
  base::JSONWriter::Write(value, &out);
  return out;
}

std::string Json(const base::DictValue& value) {
  std::string out;
  base::JSONWriter::Write(value, &out);
  return out;
}

// "--name value" and "--name=value"; nullopt when --name is absent.
std::optional<std::string> Flag(const std::vector<std::string>& argv,
                                std::string_view name) {
  const std::string bare = base::StrCat({"--", name});
  const std::string with_value = base::StrCat({bare, "="});
  for (size_t i = 1; i < argv.size(); ++i) {
    if (argv[i] == bare) {
      if (i + 1 < argv.size() && !argv[i + 1].starts_with("--")) {
        return argv[i + 1];
      }
      return std::string();
    }
    if (argv[i].starts_with(with_value)) {
      return argv[i].substr(with_value.size());
    }
  }
  return std::nullopt;
}

// LANG=en_US.UTF-8 -> en-US.
std::string Locale() {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  std::string lang = env->GetVar("LANG").value_or("en_US");
  lang = lang.substr(0, lang.find('.'));
  if (lang.empty() || lang == "C" || lang == "POSIX") {
    return "en-US";
  }
  base::ReplaceChars(lang, "_", "-", &lang);
  return lang;
}

class ProbeDelegate : public PluginHost::Delegate {
 public:
  void OnTree(const std::string& plugin,
              const std::string& surface,
              const base::DictValue& tree,
              const base::DictValue& snapshot) override {
    Print(base::StrCat({"surface/setTree ", surface, " ", Json(tree)}));
  }
  void OnSnapshot(const std::string& plugin,
                  const base::DictValue& snapshot) override {
    Print(base::StrCat({"snapshot ", Json(snapshot)}));
  }
  void OnSourceSnapshot(const std::string& source,
                        const base::Value& data) override {
    Print(base::StrCat({"source/snapshot ", source, " ", Json(data)}));
  }
  void OnNotify(const std::string& plugin,
                const PluginNotification& n) override {
    Print(base::StrCat({"notify ", Json(base::DictValue()
                                            .Set("summary", n.summary)
                                            .Set("body", n.body)
                                            .Set("icon", n.icon)
                                            .Set("urgency", n.urgency))}));
  }
  void OnToast(const std::string& plugin, const PluginToast& t) override {
    Print(base::StrCat(
        {"toast ",
         Json(base::DictValue().Set("text", t.text).Set("icon", t.icon))}));
  }
  void OnCrash(const std::string& plugin,
               const ProcessPlugin::Crash& crash) override {
    Print(base::StrCat(
        {"crash ", crash.reason, " exit ", base::NumberToString(crash.exit_code),
         " restart_in ",
         crash.restart_in
             ? base::NumberToString(crash.restart_in->InMilliseconds())
             : std::string("never")}));
  }
  void OnOpenControlPage(const std::string& plugin,
                         const std::string& page) override {
    Print(base::StrCat({"open ", page}));
  }
};

int Main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_STDERR;
  logging::InitLogging(settings);
  base::FeatureList::InitInstance(std::string(), std::string());

  // The raw argv: base::CommandLine moves positional values after switches.
  const std::vector<std::string> args(argv, UNSAFE_BUFFERS(argv + argc));
  const std::optional<std::string> dir = Flag(args, "plugin");
  const std::optional<std::string> invoke = Flag(args, "invoke");
  const std::string invoke_args = Flag(args, "args").value_or("{}");
  int seconds = 5;
  if (std::optional<std::string> s = Flag(args, "seconds");
      s && (!base::StringToInt(*s, &seconds) || seconds <= 0)) {
    seconds = -1;
  }
  std::optional<base::Value> parsed_args =
      base::JSONReader::Read(invoke_args, base::JSON_PARSE_RFC);
  if (!dir || dir->empty() || seconds <= 0 || (invoke && invoke->empty()) ||
      !parsed_args || !parsed_args->is_dict()) {
    base::WriteFileDescriptor(STDERR_FILENO, kUsage);
    return 2;
  }
  CapabilitySet capabilities;
  for (std::string_view capability :
       base::SplitStringPiece(Flag(args, "capabilities").value_or(""), ",",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    capabilities.insert(std::string(capability));
  }

  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("plugin_probe");

  PluginRegistry registry(capabilities);
  registry.Discover({base::MakeAbsoluteFilePath(base::FilePath(*dir))});
  for (const RegistryRejection& rejection : registry.rejected()) {
    Print(base::StrCat({"REJECT ", rejection.dir.value(), " ",
                        rejection.reason}));
  }
  std::vector<const PluginManifest*> plugins = registry.plugins();
  if (plugins.size() != 1) {
    Print("not registered");
    base::ThreadPoolInstance::Get()->Shutdown();
    return 1;
  }
  const PluginManifest& plugin = *plugins.front();
  Print(base::StrCat({"REGISTRY ", plugin.id}));
  base::WriteFileDescriptor(
      STDOUT_FILENO,
      WriteRegistryJson(base::Value(registry.ViewOf(plugin.id)->Clone())));

  ProbeDelegate delegate;
  PluginHost::Options options;
  options.process.locale = Locale();
  PluginHost host(&registry, &delegate, /*wm=*/nullptr, std::move(options));
  host.Activate(plugin.id);

  bool invoke_ok = !invoke.has_value();
  if (invoke) {
    const std::string qualified = plugin.Qualify(*invoke);
    host.Invoke(
        qualified, std::move(parsed_args->GetDict()), "cli",
        base::BindOnce(
            [](bool* ok, std::string command, std::string args_json,
               JsonRpcResult result) {
              if (result.has_value()) {
                *ok = true;
                Print(base::StrCat({"invoke ", command, " ", args_json,
                                    " -> result ", Json(*result)}));
              } else {
                Print(base::StrCat(
                    {"invoke ", command, " ", args_json, " -> error ",
                     base::NumberToString(result.error().code), " ",
                     result.error().message}));
              }
            },
            &invoke_ok, qualified, invoke_args));
  }

  base::RunLoop wait;
  executor.task_runner()->PostDelayedTask(FROM_HERE, wait.QuitClosure(),
                                          base::Seconds(seconds));
  wait.Run();

  bool alive = true;
  if (ProcessPlugin* process = host.process_plugin(plugin.id)) {
    alive = process->IsAlive();
    Print(base::StrCat({"plugin alive: ", alive ? "true" : "false"}));
    Print(base::StrCat({"launch path: ", ProcessPlugin::LaunchPathName(
                                             process->launch_path())}));
    base::RunLoop shutdown;
    process->Shutdown(base::BindOnce(
        [](base::OnceClosure quit, int exit_code) {
          Print(base::StrCat(
              {"shutdown exit ", base::NumberToString(exit_code)}));
          std::move(quit).Run();
        },
        shutdown.QuitClosure()));
    shutdown.Run();
  }
  base::ThreadPoolInstance::Get()->Shutdown();
  return invoke_ok && alive ? 0 : 1;
}

}  // namespace
}  // namespace views_shell

int main(int argc, char** argv) {
  return views_shell::Main(argc, argv);
}
