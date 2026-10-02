// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/permissions_broker.h"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_view_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {

PluginNotification::PluginNotification() = default;
PluginNotification::PluginNotification(const PluginNotification&) = default;
PluginNotification::PluginNotification(PluginNotification&&) = default;
PluginNotification& PluginNotification::operator=(const PluginNotification&) =
    default;
PluginNotification& PluginNotification::operator=(PluginNotification&&) =
    default;
PluginNotification::~PluginNotification() = default;

PluginToast::PluginToast() = default;
PluginToast::PluginToast(const PluginToast&) = default;
PluginToast::PluginToast(PluginToast&&) = default;
PluginToast& PluginToast::operator=(const PluginToast&) = default;
PluginToast& PluginToast::operator=(PluginToast&&) = default;
PluginToast::~PluginToast() = default;

ExecOutcome::ExecOutcome() = default;
ExecOutcome::ExecOutcome(const ExecOutcome&) = default;
ExecOutcome::ExecOutcome(ExecOutcome&&) = default;
ExecOutcome& ExecOutcome::operator=(const ExecOutcome&) = default;
ExecOutcome& ExecOutcome::operator=(ExecOutcome&&) = default;
ExecOutcome::~ExecOutcome() = default;

base::DictValue ExecOutcome::ToDict() const {
  return base::DictValue()
      .Set("exitCode", exit_code)
      .Set("stdout", stdout_text)
      .Set("stderr", stderr_text);
}

namespace {

JsonRpcError NotDeclared(const std::string& permission) {
  return JsonRpcError(kJsonRpcPermissionNotDeclared, "permission not declared",
                      base::Value(base::DictValue().Set("permission",
                                                        permission)));
}

JsonRpcError InvalidParams(std::string message) {
  return JsonRpcError(kJsonRpcInvalidParams, std::move(message));
}

// An id argument: a string, or a number that is spelled as one.
std::optional<std::string> IdArg(const base::DictValue& args,
                                 std::string_view key) {
  const base::Value* value = args.Find(key);
  if (!value) {
    return std::nullopt;
  }
  if (const std::string* text = value->GetIfString()) {
    return text->empty() ? std::nullopt : std::optional<std::string>(*text);
  }
  if (value->is_int()) {
    return base::NumberToString(value->GetInt());
  }
  return std::nullopt;
}

std::string OptionalText(const base::DictValue& params, std::string_view key) {
  const std::string* text = params.FindString(key);
  return text ? *text : std::string();
}

}  // namespace

PermissionsBroker::PermissionsBroker(Client* client, WmCommandSink* wm)
    : client_(client), wm_(wm) {}

PermissionsBroker::~PermissionsBroker() = default;

// static
std::optional<std::string> PermissionsBroker::PermissionFor(
    std::string_view method,
    const base::DictValue& params) {
  if (method == "notify") {
    return "notifications";
  }
  if (method == "exec") {
    return base::StrCat({"exec:", OptionalText(params, "program")});
  }
  if (method == "compositor/command") {
    return base::StrCat({"compositor:", OptionalText(params, "capability")});
  }
  return std::nullopt;
}

// static
bool PermissionsBroker::AllowsCall(const PluginManifest& plugin,
                                   std::string_view qualified_command) {
  return CommandReferenceCovered(plugin.id, plugin.permissions,
                                 qualified_command);
}

// static
bool PermissionsBroker::AllowsStateRead(const PluginManifest& plugin,
                                        std::string_view qualified_source) {
  const size_t slash = qualified_source.find('/');
  if (slash != std::string_view::npos &&
      qualified_source.substr(0, slash) == plugin.id) {
    return true;
  }
  return plugin.HasPermission(base::StrCat({"state:read:", qualified_source}));
}

// static
base::expected<WmCommand, std::string> PermissionsBroker::ToWmCommand(
    std::string_view capability,
    const base::DictValue& args) {
  using Kind = WmCommand::Kind;
  for (Kind kind :
       {Kind::kFocusWorkspace, Kind::kFocusWindow, Kind::kRenameWorkspace,
        Kind::kMoveWindowToWorkspace, Kind::kToggleScratchpad,
        Kind::kSessionExit, Kind::kReloadConfig}) {
    if (RequiredCapability(kind) != capability) {
      continue;
    }
    WmCommand command(kind);
    command.workspace = IdArg(args, "workspace").value_or(std::string());
    command.window = IdArg(args, "window").value_or(std::string());
    command.name = OptionalText(args, "name");
    switch (kind) {
      case Kind::kFocusWorkspace:
        if (command.workspace.empty() && command.name.empty()) {
          return base::unexpected("workspaces.focus needs workspace or name");
        }
        break;
      case Kind::kFocusWindow:
        if (command.window.empty()) {
          return base::unexpected("windows.focus needs window");
        }
        break;
      case Kind::kRenameWorkspace:
        if (command.workspace.empty() || command.name.empty()) {
          return base::unexpected(
              "workspaces.rename needs workspace and name");
        }
        break;
      case Kind::kMoveWindowToWorkspace:
        if (command.window.empty() ||
            (command.workspace.empty() && command.name.empty())) {
          return base::unexpected(
              "windows.move-to-workspace needs window, and workspace or name");
        }
        break;
      case Kind::kToggleScratchpad:
      case Kind::kSessionExit:
      case Kind::kReloadConfig:
        break;
    }
    return command;
  }
  return base::unexpected(base::StrCat(
      {"no typed compositor command for capability ", capability}));
}

void PermissionsBroker::HandleRequest(const PluginManifest& plugin,
                                      const std::string& method,
                                      const base::DictValue& params,
                                      Reply reply) {
  if (method != "notify" && method != "toast" && method != "exec" &&
      method != "compositor/command") {
    std::move(reply).Run(base::unexpected(JsonRpcError(
        kJsonRpcMethodNotFound, base::StrCat({"method not found: ", method}))));
    return;
  }
  if (method == "exec") {
    // The shape first: a request without a program names no permission.
    const std::string* program = params.FindString("program");
    const base::Value* args = params.Find("args");
    std::vector<std::string> argv;
    bool args_ok = !args || args->is_list();
    if (args && args->is_list()) {
      for (const base::Value& arg : args->GetList()) {
        args_ok = args_ok && arg.is_string();
        if (arg.is_string()) {
          argv.push_back(arg.GetString());
        }
      }
    }
    if (!program || program->empty() || !args_ok) {
      std::move(reply).Run(base::unexpected(
          InvalidParams("exec needs {program: string, args: [string]}")));
      return;
    }
    const std::string permission = base::StrCat({"exec:", *program});
    if (!plugin.HasPermission(permission)) {
      LOG(WARNING) << "plugin " << plugin.id << ": refused exec " << *program
                   << " (" << permission << " not declared)";
      std::move(reply).Run(base::unexpected(NotDeclared(permission)));
      return;
    }
    RunProgramAsync(*program, argv,
                    base::BindOnce(
                        [](Reply answer, ExecOutcome outcome) {
                          std::move(answer).Run(base::Value(outcome.ToDict()));
                        },
                        std::move(reply)));
    return;
  }

  if (std::optional<std::string> permission = PermissionFor(method, params);
      permission && !plugin.HasPermission(*permission)) {
    LOG(WARNING) << "plugin " << plugin.id << ": refused " << method << " ("
                 << *permission << " not declared)";
    std::move(reply).Run(base::unexpected(NotDeclared(*permission)));
    return;
  }

  if (method == "notify") {
    const std::string* summary = params.FindString("summary");
    PluginNotification notification;
    notification.summary = summary ? *summary : std::string();
    notification.body = OptionalText(params, "body");
    notification.icon = OptionalText(params, "icon");
    if (const std::string* urgency = params.FindString("urgency")) {
      notification.urgency = *urgency;
    }
    if (!summary || (notification.urgency != "low" &&
                     notification.urgency != "normal" &&
                     notification.urgency != "critical")) {
      std::move(reply).Run(base::unexpected(InvalidParams(
          "notify needs {summary: string, body?, icon?, "
          "urgency?: low|normal|critical}")));
      return;
    }
    client_->OnNotify(plugin, notification);
    std::move(reply).Run(base::Value(base::DictValue()));
    return;
  }

  if (method == "toast") {
    const std::string* text = params.FindString("text");
    if (!text) {
      std::move(reply).Run(
          base::unexpected(InvalidParams("toast needs {text: string}")));
      return;
    }
    PluginToast toast;
    toast.text = *text;
    toast.icon = OptionalText(params, "icon");
    client_->OnToast(plugin, toast);
    std::move(reply).Run(base::Value(base::DictValue()));
    return;
  }

  // compositor/command
  const base::DictValue* args = params.FindDict("args");
  const base::DictValue no_args;
  base::expected<WmCommand, std::string> command =
      ToWmCommand(OptionalText(params, "capability"), args ? *args : no_args);
  if (!command.has_value()) {
    std::move(reply).Run(base::unexpected(InvalidParams(command.error())));
    return;
  }
  if (!wm_) {
    std::move(reply).Run(base::unexpected(
        JsonRpcError(kJsonRpcRequestFailed, "no compositor adapter")));
    return;
  }
  wm_->Send(*command,
            base::BindOnce(
                [](Reply answer, base::expected<void, WmCommandError> done) {
                  if (done.has_value()) {
                    std::move(answer).Run(base::Value(base::DictValue()));
                    return;
                  }
                  const std::string name(WmCommandErrorName(done.error()));
                  std::move(answer).Run(base::unexpected(JsonRpcError(
                      kJsonRpcRequestFailed,
                      base::StrCat({"compositor command failed: ", name}),
                      base::Value(base::DictValue().Set("error", name)))));
                },
                std::move(reply)));
}

// static
ExecOutcome PermissionsBroker::RunProgram(const std::string& program,
                                          const std::vector<std::string>& args,
                                          base::TimeDelta deadline) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  ExecOutcome outcome;
  base::ScopedFD out_read, out_write, err_read, err_write;
  if (!base::CreatePipe(&out_read, &out_write) ||
      !base::CreatePipe(&err_read, &err_write)) {
    outcome.exit_code = 127;
    outcome.stderr_text = "views-shell: cannot create pipes";
    return outcome;
  }
  std::vector<std::string> argv = {program};
  argv.insert(argv.end(), args.begin(), args.end());
  base::LaunchOptions options;
  options.fds_to_remap.emplace_back(out_write.get(), STDOUT_FILENO);
  options.fds_to_remap.emplace_back(err_write.get(), STDERR_FILENO);
  base::Process process = base::LaunchProcess(argv, options);
  out_write.reset();
  err_write.reset();
  if (!process.IsValid()) {
    outcome.exit_code = 127;
    outcome.stderr_text =
        base::StrCat({"views-shell: cannot run ", program});
    return outcome;
  }

  const base::TimeTicks end = base::TimeTicks::Now() + deadline;
  std::array<pollfd, 2> fds = {{{out_read.get(), POLLIN, 0},
                                {err_read.get(), POLLIN, 0}}};
  std::array<std::string*, 2> sinks = {&outcome.stdout_text,
                                       &outcome.stderr_text};
  std::array<char, 4096> buf;
  bool timed_out = false;
  while (fds[0].fd >= 0 || fds[1].fd >= 0) {
    const base::TimeDelta left = end - base::TimeTicks::Now();
    if (left <= base::TimeDelta()) {
      timed_out = true;
      break;
    }
    const int ready = HANDLE_EINTR(
        poll(fds.data(), fds.size(), static_cast<int>(left.InMilliseconds())));
    if (ready <= 0) {
      timed_out = ready == 0;
      break;
    }
    for (size_t i = 0; i < fds.size(); ++i) {
      if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
        continue;
      }
      const ssize_t n = HANDLE_EINTR(read(fds[i].fd, buf.data(), buf.size()));
      if (n <= 0) {
        fds[i].fd = -1;
        continue;
      }
      std::string* sink = sinks[i];
      const size_t room = kExecOutputCap - std::min(kExecOutputCap, sink->size());
      sink->append(base::as_string_view(base::span(buf).first(
          std::min(room, static_cast<size_t>(n)))));
    }
  }
  int exit_code = 0;
  if (timed_out ||
      !process.WaitForExitWithTimeout(
          std::max(end - base::TimeTicks::Now(), base::Milliseconds(100)),
          &exit_code)) {
    process.Terminate(137, /*wait=*/true);
    outcome.exit_code = 137;
    outcome.stderr_text +=
        base::StrCat({"views-shell: ", program, " killed at the ",
                      base::NumberToString(deadline.InSeconds()),
                      " s deadline"});
    return outcome;
  }
  outcome.exit_code = exit_code;
  return outcome;
}

// static
void PermissionsBroker::RunProgramAsync(
    const std::string& program,
    const std::vector<std::string>& args,
    base::OnceCallback<void(ExecOutcome)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&PermissionsBroker::RunProgram, program, args,
                     kExecDeadline),
      std::move(done));
}

}  // namespace views_shell
