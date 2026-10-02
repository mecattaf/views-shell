// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/plugins/process_plugin.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cerrno>
#include <utility>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_view_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "views_shell/plugins/permissions_broker.h"
#include "views_shell/plugins/plugin_manifest.h"

namespace views_shell {

namespace {

constexpr size_t kStderrTailBytes = 2048;

// The launch-path probe runs once per process; its answer and the
// systemd-run it found are shared by every plugin.
struct ScopeProbe {
  base::Lock lock;
  bool done = false;
  bool works = false;
  std::string systemd_run;
  std::string why;
};

ScopeProbe& Probe() {
  static base::NoDestructor<ScopeProbe> probe;
  return *probe;
}

std::string FindOnPath(std::string_view program) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  const std::string path = env->GetVar("PATH").value_or("/usr/bin:/bin");
  for (std::string_view dir : base::SplitStringPiece(
           path, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const base::FilePath candidate = base::FilePath(dir).Append(program);
    if (access(candidate.value().c_str(), X_OK) == 0 &&
        !base::DirectoryExists(candidate)) {
      return candidate.value();
    }
  }
  return std::string();
}

// Blocking: finds systemd-run and checks that a user scope can be created.
ProcessPlugin::LaunchPath ProbeScope() {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  ScopeProbe& probe = Probe();
  base::AutoLock hold(probe.lock);
  if (!probe.done) {
    probe.done = true;
    probe.systemd_run = FindOnPath("systemd-run");
    if (probe.systemd_run.empty()) {
      probe.why = "systemd-run is not on PATH";
    } else {
      base::ScopedFD null_fd(HANDLE_EINTR(open("/dev/null", O_RDWR)));
      base::LaunchOptions options;
      if (null_fd.is_valid()) {
        options.fds_to_remap.emplace_back(null_fd.get(), STDOUT_FILENO);
        options.fds_to_remap.emplace_back(null_fd.get(), STDERR_FILENO);
      }
      base::Process process = base::LaunchProcess(
          std::vector<std::string>{probe.systemd_run, "--user", "--scope",
                                   "--quiet", "--collect", "--", "true"},
          options);
      int exit_code = -1;
      if (!process.IsValid()) {
        probe.why = "systemd-run could not be started";
      } else if (!process.WaitForExitWithTimeout(base::Seconds(5),
                                                 &exit_code)) {
        process.Terminate(-1, /*wait=*/true);
        probe.why = "systemd-run --user --scope did not finish within 5 s";
      } else if (exit_code != 0) {
        probe.why = base::StrCat(
            {"systemd-run --user --scope true exited ",
             base::NumberToString(exit_code), " (no reachable user manager)"});
      } else {
        probe.works = true;
      }
    }
  }
  return probe.works ? ProcessPlugin::LaunchPath::kScope
                     : ProcessPlugin::LaunchPath::kPlain;
}

// A pipe write to a plugin that already exited must not kill views-shell.
void IgnoreSigpipeOnce() {
  static std::atomic<bool> done{false};
  if (!done.exchange(true)) {
    signal(SIGPIPE, SIG_IGN);
  }
}

bool ArgMatchesType(const base::Value& value, std::string_view type) {
  if (type == "string") {
    return value.is_string();
  }
  if (type == "boolean") {
    return value.is_bool();
  }
  if (type == "number") {
    return value.is_int() || value.is_double();
  }
  // integer
  return value.is_int() ||
         (value.is_double() && std::isfinite(value.GetDouble()) &&
          value.GetDouble() == std::floor(value.GetDouble()));
}

JsonRpcError Failed(std::string message) {
  return JsonRpcError(kJsonRpcRequestFailed, std::move(message));
}

}  // namespace

ProcessPlugin::Options::Options() = default;
ProcessPlugin::Options::Options(Options&&) = default;
ProcessPlugin::Options& ProcessPlugin::Options::operator=(Options&&) = default;
ProcessPlugin::Options::~Options() = default;

ProcessPlugin::Crash::Crash() = default;
ProcessPlugin::Crash::Crash(const Crash&) = default;
ProcessPlugin::Crash::Crash(Crash&&) = default;
ProcessPlugin::Crash& ProcessPlugin::Crash::operator=(const Crash&) = default;
ProcessPlugin::Crash& ProcessPlugin::Crash::operator=(Crash&&) = default;
ProcessPlugin::Crash::~Crash() = default;

ProcessPlugin::Pending::Pending() = default;
ProcessPlugin::Pending::Pending(Pending&&) = default;
ProcessPlugin::Pending& ProcessPlugin::Pending::operator=(Pending&&) = default;
ProcessPlugin::Pending::~Pending() = default;

ProcessPlugin::Queued::Queued() = default;
ProcessPlugin::Queued::Queued(Queued&&) = default;
ProcessPlugin::Queued& ProcessPlugin::Queued::operator=(Queued&&) = default;
ProcessPlugin::Queued::~Queued() = default;

ProcessPlugin::ProcessPlugin(const PluginManifest& manifest,
                             Options options,
                             TreeSink* sink,
                             PermissionsBroker* broker,
                             Observer* observer)
    : manifest_(manifest),
      options_(std::move(options)),
      sink_(sink),
      broker_(broker),
      observer_(observer) {
  CHECK(manifest.tier == PluginTier::kProcess);
}

ProcessPlugin::~ProcessPlugin() {
  DCHECK_CALLING_ON_VALID_SEQUENCE(sequence_checker_);
  if (process_.IsValid()) {
    // No grace here: Shutdown() is the orderly path. Reap on the pool so no
    // zombie is left behind.
    ::kill(process_.Pid(), SIGKILL);
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(),
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce([](base::Process process) { process.WaitForExit(nullptr); },
                       std::move(process_)));
  }
}

// static
std::string_view ProcessPlugin::LaunchPathName(LaunchPath path) {
  switch (path) {
    case LaunchPath::kAuto:
      return "auto";
    case LaunchPath::kScope:
      return "scope";
    case LaunchPath::kPlain:
      return "plain";
  }
}

// static
std::string_view ProcessPlugin::StateName(State state) {
  switch (state) {
    case State::kStopped:
      return "stopped";
    case State::kStarting:
      return "starting";
    case State::kRunning:
      return "running";
    case State::kRestarting:
      return "restarting";
    case State::kFailed:
      return "failed";
    case State::kShuttingDown:
      return "shutting-down";
    case State::kExited:
      return "exited";
  }
}

bool ProcessPlugin::IsAlive() const {
  return state_ == State::kRunning && !reaping_ && process_.IsValid();
}

base::DictValue ProcessPlugin::EffectiveConfig() const {
  base::DictValue config = manifest_->config_defaults.Clone();
  config.Merge(options_.config.Clone());
  return config;
}

void ProcessPlugin::SetState(State state) {
  if (state_ != state) {
    VLOG(1) << "plugin " << manifest_->id << ": " << StateName(state_)
            << " -> " << StateName(state);
  }
  state_ = state;
}

void ProcessPlugin::Start() {
  DCHECK_CALLING_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kStopped && state_ != State::kExited &&
      state_ != State::kFailed) {
    return;
  }
  IgnoreSigpipeOnce();
  consecutive_crashes_ = 0;
  SetState(State::kStarting);
  if (options_.launch_path != LaunchPath::kAuto) {
    OnLaunchPathKnown(options_.launch_path);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&ProbeScope),
      base::BindOnce(&ProcessPlugin::OnLaunchPathKnown,
                     weak_factory_.GetWeakPtr()));
}

void ProcessPlugin::OnLaunchPathKnown(LaunchPath path) {
  if (state_ != State::kStarting) {
    return;  // shut down while the probe ran
  }
  if (path == LaunchPath::kPlain && options_.launch_path == LaunchPath::kAuto) {
    ScopeProbe& probe = Probe();
    base::AutoLock hold(probe.lock);
    LOG(INFO) << "plugin " << manifest_->id
              << ": no systemd user scope (" << probe.why
              << "); launching with base::LaunchProcess";
  }
  Launch(path);
}

void ProcessPlugin::Launch(LaunchPath path) {
  DCHECK_CALLING_ON_VALID_SEQUENCE(sequence_checker_);
  launch_path_ = path;
  fail_reason_.clear();
  protocol_mismatch_ = false;
  trees_seen_.clear();
  snapshot_seen_ = false;
  stderr_tail_.clear();
  ++launches_;
  SetState(State::kStarting);

  base::ScopedFD in_read, in_write, out_read, out_write, err_read, err_write;
  if (!base::CreatePipe(&in_read, &in_write) ||
      !base::CreatePipe(&out_read, &out_write) ||
      !base::CreatePipe(&err_read, &err_write)) {
    fail_reason_ = "cannot create pipes";
    OnExited(fail_reason_, -1);
    return;
  }
  const std::string exec =
      manifest_->dir.Append(base::FilePath(manifest_->exec)).value();
  std::vector<std::string> argv;
  std::string unit;
  if (path == LaunchPath::kScope) {
    std::string systemd_run;
    {
      ScopeProbe& probe = Probe();
      base::AutoLock hold(probe.lock);
      systemd_run = probe.systemd_run.empty() ? std::string("systemd-run")
                                              : probe.systemd_run;
    }
    unit = base::StrCat({"views-shell-plugin-", manifest_->id, "-",
                         base::NumberToString(getpid()), "-",
                         base::NumberToString(launches_)});
    argv = {systemd_run, "--user",  "--scope",
            "--quiet",   "--collect", base::StrCat({"--unit=", unit}),
            "--"};
  }
  argv.push_back(exec);
  argv.insert(argv.end(), manifest_->exec_args.begin(),
              manifest_->exec_args.end());

  base::LaunchOptions options;
  options.current_directory = manifest_->dir;
  options.fds_to_remap.emplace_back(in_read.get(), STDIN_FILENO);
  options.fds_to_remap.emplace_back(out_write.get(), STDOUT_FILENO);
  options.fds_to_remap.emplace_back(err_write.get(), STDERR_FILENO);
  process_ = base::LaunchProcess(argv, options);
  in_read.reset();
  out_write.reset();
  err_write.reset();
  launched_at_ = base::TimeTicks::Now();
  if (!process_.IsValid()) {
    fail_reason_ = base::StrCat({"cannot launch ", exec});
    OnExited(fail_reason_, -1);
    return;
  }
  pid_ = process_.Pid();
  if (path == LaunchPath::kScope) {
    LOG(INFO) << "plugin " << manifest_->id << ": launched pid " << pid_
              << " via systemd-run --user --scope (unit " << unit
              << ".scope)";
  } else {
    LOG(INFO) << "plugin " << manifest_->id << ": launched pid " << pid_
              << " via base::LaunchProcess (plain path)";
  }

  stdin_ = std::move(in_write);
  stdout_ = std::move(out_read);
  stderr_ = std::move(err_read);
  base::SetNonBlocking(stdin_.get());
  base::SetNonBlocking(stdout_.get());
  base::SetNonBlocking(stderr_.get());
  stdout_watch_ = base::FileDescriptorWatcher::WatchReadable(
      stdout_.get(), base::BindRepeating(&ProcessPlugin::OnStdoutReadable,
                                         weak_factory_.GetWeakPtr()));
  stderr_watch_ = base::FileDescriptorWatcher::WatchReadable(
      stderr_.get(), base::BindRepeating(&ProcessPlugin::OnStderrReadable,
                                         weak_factory_.GetWeakPtr()));

  base::ListValue capabilities;
  for (const Capability& capability : options_.capabilities) {
    if (manifest_->required_capabilities.contains(capability) ||
        manifest_->optional_capabilities.contains(capability)) {
      capabilities.Append(capability);
    }
  }
  SendRequest("initialize",
              base::DictValue()
                  .Set("protocol", kPluginProtocolVersion)
                  .Set("shellVersion", options_.shell_version)
                  .Set("pluginId", manifest_->id)
                  .Set("capabilities", std::move(capabilities))
                  .Set("config", EffectiveConfig())
                  .Set("locale", options_.locale),
              options_.request_timeout,
              base::BindOnce(&ProcessPlugin::OnInitializeAnswer,
                             weak_factory_.GetWeakPtr()));
  startup_timer_.Start(FROM_HERE, options_.startup_window,
                       base::BindOnce(&ProcessPlugin::OnStartupWindowEnd,
                                      weak_factory_.GetWeakPtr()));
}

void ProcessPlugin::OnStdoutReadable() {
  base::WeakPtr<ProcessPlugin> weak = weak_factory_.GetWeakPtr();
  std::array<char, 65536> buf;
  while (stdout_.is_valid()) {
    const ssize_t n = HANDLE_EINTR(read(stdout_.get(), buf.data(), buf.size()));
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (n <= 0) {
      stdout_watch_.reset();
      stdout_.reset();
      std::string rest = stdout_reader_.TakeRemainder();
      if (!rest.empty()) {
        HandleLine(rest);
        if (!weak) {
          return;
        }
      }
      OnStreamsClosed();
      return;
    }
    for (const std::string& line : stdout_reader_.Append(base::as_string_view(
             base::span(buf).first(static_cast<size_t>(n))))) {
      HandleLine(line);
      if (!weak) {
        return;
      }
    }
  }
}

void ProcessPlugin::OnStderrReadable() {
  std::array<char, 4096> buf;
  while (stderr_.is_valid()) {
    const ssize_t n = HANDLE_EINTR(read(stderr_.get(), buf.data(), buf.size()));
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (n <= 0) {
      stderr_watch_.reset();
      stderr_.reset();
      return;
    }
    const std::string_view chunk =
        base::as_string_view(base::span(buf).first(static_cast<size_t>(n)));
    stderr_tail_.append(chunk);
    if (stderr_tail_.size() > kStderrTailBytes) {
      stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytes);
    }
    for (const std::string& line : stderr_reader_.Append(chunk)) {
      LOG(INFO) << "plugin " << manifest_->id << " stderr: " << line;
    }
  }
}

void ProcessPlugin::Write(std::string line) {
  if (!stdin_.is_valid()) {
    return;
  }
  write_buffer_.append(line);
  FlushWrites();
}

void ProcessPlugin::FlushWrites() {
  while (!write_buffer_.empty() && stdin_.is_valid()) {
    const ssize_t n = HANDLE_EINTR(
        write(stdin_.get(), write_buffer_.data(), write_buffer_.size()));
    if (n > 0) {
      write_buffer_.erase(0, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!stdin_watch_) {
        stdin_watch_ = base::FileDescriptorWatcher::WatchWritable(
            stdin_.get(), base::BindRepeating(&ProcessPlugin::OnWritable,
                                              weak_factory_.GetWeakPtr()));
      }
      return;
    }
    // EPIPE: the plugin closed its stdin or exited; stdout's end of file
    // reports the exit.
    write_buffer_.clear();
    CloseStdin();
    return;
  }
  stdin_watch_.reset();
}

void ProcessPlugin::OnWritable() {
  FlushWrites();
}

void ProcessPlugin::CloseStdin() {
  stdin_watch_.reset();
  stdin_.reset();
}

void ProcessPlugin::KillNow() {
  if (process_.IsValid()) {
    ::kill(process_.Pid(), SIGKILL);
  }
}

void ProcessPlugin::HandleLine(const std::string& line) {
  base::expected<JsonRpcMessage, std::string> parsed = ParseJsonRpcLine(line);
  if (!parsed.has_value()) {
    LOG(WARNING) << "plugin " << manifest_->id << ": dropped a line ("
                 << parsed.error() << "): " << line.substr(0, 120);
    return;
  }
  JsonRpcMessage message = std::move(parsed).value();
  if (message.is_answer()) {
    HandleAnswer(std::move(message));
    return;
  }
  if (state_ == State::kStarting) {
    LOG(WARNING) << "plugin " << manifest_->id << ": dropped " << message.method
                 << ", sent before the initialize answer";
    return;
  }
  if (message.kind == JsonRpcMessage::Kind::kNotification) {
    HandleNotification(std::move(message));
  } else {
    HandleRequest(std::move(message));
  }
}

void ProcessPlugin::HandleAnswer(JsonRpcMessage message) {
  auto it = pending_.find(JsonRpcIdToString(message.id));
  if (!message.id.is_string() || it == pending_.end()) {
    LOG(WARNING) << "plugin " << manifest_->id << ": answer to id "
                 << JsonRpcIdToString(message.id)
                 << ", which the host never sent or already settled";
    return;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);
  if (message.kind == JsonRpcMessage::Kind::kResult) {
    std::move(pending.callback).Run(std::move(message.result));
  } else {
    std::move(pending.callback).Run(base::unexpected(std::move(message.error)));
  }
}

void ProcessPlugin::HandleNotification(JsonRpcMessage message) {
  const std::string& method = message.method;
  base::DictValue& params = message.params;
  if (method == "snapshot") {
    base::DictValue* data = params.FindDict("data");
    if (!data) {
      LOG(WARNING) << "plugin " << manifest_->id
                   << ": dropped a snapshot whose data is not an object";
      return;
    }
    snapshot_seen_ = true;
    sink_->OnSnapshot(*manifest_, std::move(*data));
    return;
  }
  if (method == "surface/setTree") {
    const std::string* surface = params.FindString("surface");
    base::DictValue* tree = params.FindDict("tree");
    const std::vector<std::string> surfaces = manifest_->TreeSurfaces();
    if (!surface || !tree || std::ranges::find(surfaces, *surface) ==
                                 surfaces.end()) {
      LOG(WARNING) << "plugin " << manifest_->id
                   << ": dropped surface/setTree for a surface the manifest "
                      "does not contribute ("
                   << (surface ? *surface : std::string("none")) << ")";
      return;
    }
    if (std::ranges::find(trees_seen_, *surface) == trees_seen_.end()) {
      trees_seen_.push_back(*surface);
    }
    sink_->OnTree(*manifest_, *surface, std::move(*tree));
    return;
  }
  if (method == "source/snapshot") {
    const std::string* source = params.FindString("source");
    base::Value* data = params.Find("data");
    if (!source || !data ||
        std::ranges::find(manifest_->sources, *source) ==
            manifest_->sources.end()) {
      LOG(WARNING) << "plugin " << manifest_->id
                   << ": dropped source/snapshot for an undeclared source";
      return;
    }
    sink_->OnSourceSnapshot(*manifest_,
                            base::StrCat({manifest_->id, "/", *source}),
                            std::move(*data));
    return;
  }
  LOG(WARNING) << "plugin " << manifest_->id << ": ignored notification "
               << method
               << (method == "notify" || method == "toast" || method == "exec" ||
                           method == "compositor/command"
                       ? " (a request: it needs an id)"
                       : "");
}

void ProcessPlugin::HandleRequest(JsonRpcMessage message) {
  base::Value id = std::move(message.id);
  broker_->HandleRequest(
      *manifest_, message.method, message.params,
      base::BindOnce(&ProcessPlugin::ReplyToPlugin, weak_factory_.GetWeakPtr(),
                     std::move(id)));
}

void ProcessPlugin::ReplyToPlugin(base::Value id, JsonRpcResult result) {
  if (!stdin_.is_valid()) {
    return;
  }
  if (result.has_value()) {
    Write(JsonRpcMessage::Result(std::move(id), std::move(result).value())
              .Serialize());
  } else {
    Write(JsonRpcMessage::Error(std::move(id), std::move(result).error())
              .Serialize());
  }
}

void ProcessPlugin::OnInitializeAnswer(JsonRpcResult result) {
  if (state_ != State::kStarting) {
    return;
  }
  if (!result.has_value()) {
    fail_reason_ = result.error().code == kJsonRpcRequestFailed
                       ? std::string("no initialize answer")
                       : base::StrCat({"initialize answered an error: ",
                                       result.error().message});
    KillNow();
    return;
  }
  const base::DictValue* answer = result->GetIfDict();
  const base::Value* protocol = answer ? answer->Find("protocol") : nullptr;
  if (!protocol || !protocol->is_int() ||
      protocol->GetInt() != kPluginProtocolVersion) {
    protocol_mismatch_ = true;
    fail_reason_ = "protocol mismatch";
    LOG(ERROR) << "plugin " << manifest_->id
               << ": initialize answered a protocol other than "
               << kPluginProtocolVersion << "; not starting it";
    KillNow();
    return;
  }
  SetState(State::kRunning);
  std::vector<Queued> queued = std::move(queued_);
  queued_.clear();
  for (Queued& call : queued) {
    SendRequest(std::move(call.method), std::move(call.params),
                options_.request_timeout, std::move(call.callback));
  }
  if (observer_) {
    observer_->OnPluginRunning(*this);
  }
}

void ProcessPlugin::OnStartupWindowEnd() {
  std::vector<std::string> missing;
  for (const std::string& surface : manifest_->TreeSurfaces()) {
    if (std::ranges::find(trees_seen_, surface) == trees_seen_.end()) {
      missing.push_back(surface);
    }
  }
  if (!missing.empty() || !snapshot_seen_) {
    LOG(WARNING) << "plugin " << manifest_->id << ": the "
                 << options_.startup_window.InMilliseconds()
                 << " ms startup window ended "
                 << (snapshot_seen_ ? "with a snapshot" : "without a snapshot")
                 << " and " << missing.size() << " tree(s) missing";
  }
}

void ProcessPlugin::SendRequest(std::string method,
                                base::DictValue params,
                                base::TimeDelta timeout,
                                ResultCallback callback) {
  const std::string id =
      base::StrCat({"host-", base::NumberToString(++next_request_)});
  Pending pending;
  pending.method = method;
  pending.callback = std::move(callback);
  pending.timer = std::make_unique<base::OneShotTimer>();
  pending.timer->Start(FROM_HERE, timeout,
                       base::BindOnce(&ProcessPlugin::OnRequestTimeout,
                                      weak_factory_.GetWeakPtr(), id));
  pending_.emplace(id, std::move(pending));
  Write(JsonRpcMessage::Request(base::Value(id), std::move(method),
                                std::move(params))
            .Serialize());
}

void ProcessPlugin::OnRequestTimeout(const std::string& id) {
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);
  LOG(WARNING) << "plugin " << manifest_->id << ": no answer to "
               << pending.method << " (" << id << ")";
  std::move(pending.callback)
      .Run(base::unexpected(Failed(base::StrCat(
          {"no answer to ", pending.method, " within ",
           base::NumberToString(options_.request_timeout.InMilliseconds()),
           " ms"}))));
}

void ProcessPlugin::Call(std::string method,
                         base::DictValue params,
                         ResultCallback callback) {
  switch (state_) {
    case State::kRunning:
      SendRequest(std::move(method), std::move(params),
                  options_.request_timeout, std::move(callback));
      return;
    case State::kStarting:
    case State::kRestarting: {
      Queued call;
      call.method = std::move(method);
      call.params = std::move(params);
      call.callback = std::move(callback);
      queued_.push_back(std::move(call));
      return;
    }
    case State::kStopped:
    case State::kFailed:
    case State::kShuttingDown:
    case State::kExited:
      std::move(callback).Run(base::unexpected(Failed(base::StrCat(
          {"plugin ", manifest_->id, " is not running (", StateName(state_),
           ")"}))));
      return;
  }
}

void ProcessPlugin::SendNotification(std::string method,
                                     base::DictValue params) {
  if (state_ != State::kRunning) {
    return;
  }
  Write(JsonRpcMessage::Notification(std::move(method), std::move(params))
            .Serialize());
}

void ProcessPlugin::Invoke(std::string_view command,
                           base::DictValue args,
                           std::string_view source,
                           std::optional<std::string> instance,
                           ResultCallback callback) {
  const PluginCommand* declared = manifest_->FindCommand(command);
  auto invalid = [&callback](std::string message) {
    std::move(callback).Run(base::unexpected(
        JsonRpcError(kJsonRpcInvalidParams, std::move(message))));
  };
  if (!declared) {
    invalid(base::StrCat({"command ", command, " is not declared by ",
                          manifest_->id}));
    return;
  }
  if (declared->verb) {
    invalid(base::StrCat({"command ", command, " is a surface verb (",
                          *declared->verb, "); views-shell handles it"}));
    return;
  }
  for (const PluginCommandArg& arg : declared->args) {
    const base::Value* value = args.Find(arg.name);
    if (!value) {
      if (arg.required) {
        invalid(base::StrCat({"command ", declared->id, " needs argument ",
                              arg.name}));
        return;
      }
      continue;
    }
    if (!ArgMatchesType(*value, arg.type)) {
      invalid(base::StrCat({"argument ", arg.name, " of ", declared->id,
                            " must be a ", arg.type}));
      return;
    }
  }
  for (const auto [name, unused] : args) {
    if (std::ranges::none_of(declared->args, [&name](const PluginCommandArg& a) {
          return a.name == name;
        })) {
      invalid(base::StrCat({"command ", declared->id,
                            " declares no argument ", name}));
      return;
    }
  }
  base::DictValue params;
  params.Set("command", declared->id);
  params.Set("args", std::move(args));
  params.Set("source", source);
  if (instance) {
    params.Set("instance", *instance);
  }
  // The answer must match the declared result type.
  Call("command/invoke", std::move(params),
       base::BindOnce(
           [](std::string result_type, std::string command,
              ResultCallback callback, JsonRpcResult answer) {
             if (answer.has_value()) {
               const base::DictValue* dict = answer->GetIfDict();
               const base::Value* result =
                   dict ? dict->Find("result") : nullptr;
               const bool ok =
                   dict && (result_type == "none" ||
                            (result_type == "text" && result &&
                             result->is_string()) ||
                            (result_type == "json" && result));
               if (!ok) {
                 std::move(callback).Run(base::unexpected(JsonRpcError(
                     kJsonRpcInternalError,
                     base::StrCat({"the answer to ", command,
                                   " does not match its result type ",
                                   result_type}))));
                 return;
               }
             }
             std::move(callback).Run(std::move(answer));
           },
           declared->result, declared->id, std::move(callback)));
}

void ProcessPlugin::SurfaceOpened(const std::string& surface,
                                  const std::string& instance,
                                  ResultCallback callback) {
  Call("surface/opened",
       base::DictValue().Set("surface", surface).Set("instance", instance),
       std::move(callback));
}

void ProcessPlugin::SurfaceClosed(const std::string& surface,
                                  const std::string& instance,
                                  ResultCallback callback) {
  Call("surface/closed",
       base::DictValue().Set("surface", surface).Set("instance", instance),
       std::move(callback));
}

void ProcessPlugin::ConfigChanged(base::DictValue config,
                                  ResultCallback callback) {
  options_.config = std::move(config);
  Call("config/changed", base::DictValue().Set("config", EffectiveConfig()),
       std::move(callback));
}

bool ProcessPlugin::DeliverEvent(const std::string& name, base::Value data) {
  if (std::ranges::find(manifest_->events, name) == manifest_->events.end()) {
    return false;
  }
  SendNotification("event", base::DictValue()
                                .Set("name", name)
                                .Set("data", std::move(data)));
  return true;
}

bool ProcessPlugin::DeliverSourceChanged(const std::string& source,
                                         base::Value data) {
  if (!PermissionsBroker::AllowsStateRead(*manifest_, source)) {
    return false;
  }
  SendNotification("source/changed", base::DictValue()
                                         .Set("source", source)
                                         .Set("data", std::move(data)));
  return true;
}

void ProcessPlugin::Shutdown(base::OnceCallback<void(int)> done) {
  DCHECK_CALLING_ON_VALID_SEQUENCE(sequence_checker_);
  restart_timer_.Stop();
  if ((state_ != State::kRunning && state_ != State::kStarting) ||
      !process_.IsValid() || reaping_) {
    if (state_ != State::kShuttingDown) {
      FailPending("plugin shut down");
      SetState(state_ == State::kFailed ? State::kFailed : State::kExited);
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), -1));
    return;
  }
  SetState(State::kShuttingDown);
  shutdown_done_ = std::move(done);
  SendRequest("shutdown", base::DictValue(), options_.shutdown_grace,
              base::BindOnce(
                  [](base::WeakPtr<ProcessPlugin> self, JsonRpcResult) {
                    if (self) {
                      self->CloseStdin();
                    }
                  },
                  weak_factory_.GetWeakPtr()));
  Reap(options_.shutdown_grace, "shutdown");
}

void ProcessPlugin::OnStreamsClosed() {
  if (!reaping_ && process_.IsValid()) {
    Reap(options_.shutdown_grace,
         fail_reason_.empty() ? std::string("exited") : fail_reason_);
  }
}

void ProcessPlugin::Reap(base::TimeDelta grace, std::string reason) {
  reaping_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(
          [](base::Process process, base::TimeDelta grace) {
            int exit_code = -1;
            if (!process.WaitForExitWithTimeout(grace, &exit_code)) {
              ::kill(process.Pid(), SIGKILL);
              exit_code = -1;
              process.WaitForExit(nullptr);
            }
            return exit_code;
          },
          std::move(process_), grace),
      base::BindOnce(&ProcessPlugin::OnExited, weak_factory_.GetWeakPtr(),
                     std::move(reason)));
}

void ProcessPlugin::FailPending(const std::string& why) {
  std::map<std::string, Pending> pending = std::move(pending_);
  pending_.clear();
  for (auto& [id, call] : pending) {
    std::move(call.callback).Run(base::unexpected(Failed(why)));
  }
  std::vector<Queued> queued = std::move(queued_);
  queued_.clear();
  for (Queued& call : queued) {
    std::move(call.callback).Run(base::unexpected(Failed(why)));
  }
}

void ProcessPlugin::OnExited(std::string reason, int exit_code) {
  DCHECK_CALLING_ON_VALID_SEQUENCE(sequence_checker_);
  reaping_ = false;
  process_ = base::Process();
  startup_timer_.Stop();
  stdout_watch_.reset();
  stderr_watch_.reset();
  stdout_.reset();
  stderr_.reset();
  CloseStdin();
  write_buffer_.clear();
  stdout_reader_.TakeRemainder();
  stderr_reader_.TakeRemainder();

  if (state_ == State::kShuttingDown) {
    FailPending("plugin shut down");
    SetState(State::kExited);
    LOG(INFO) << "plugin " << manifest_->id << ": shut down, exit status "
              << exit_code;
    if (shutdown_done_) {
      std::move(shutdown_done_).Run(exit_code);
    }
    return;
  }

  FailPending(base::StrCat({"plugin ", manifest_->id, " exited"}));
  if (base::TimeTicks::Now() - launched_at_ >= options_.stable_after) {
    consecutive_crashes_ = 0;
  }
  ++consecutive_crashes_;
  Crash crash;
  crash.plugin_id = manifest_->id;
  crash.exit_code = exit_code;
  crash.reason = std::move(reason);
  crash.consecutive = consecutive_crashes_;
  crash.stderr_tail = stderr_tail_;
  if (protocol_mismatch_ || consecutive_crashes_ > options_.max_restarts) {
    SetState(State::kFailed);
  } else {
    base::TimeDelta backoff = options_.initial_backoff;
    for (int i = 1; i < consecutive_crashes_ && backoff < options_.max_backoff;
         ++i) {
      backoff *= 2;
    }
    backoff = std::min(backoff, options_.max_backoff);
    crash.restart_in = backoff;
    SetState(State::kRestarting);
    restart_timer_.Start(FROM_HERE, backoff,
                         base::BindOnce(&ProcessPlugin::Launch,
                                        weak_factory_.GetWeakPtr(),
                                        launch_path_));
  }
  LOG(WARNING) << "plugin " << manifest_->id << ": " << crash.reason
               << " (exit status " << exit_code << ", crash "
               << crash.consecutive << ")"
               << (crash.restart_in
                       ? base::StrCat({"; restarting in ",
                                       base::NumberToString(
                                           crash.restart_in->InMilliseconds()),
                                       " ms"})
                       : std::string("; given up"));
  if (observer_) {
    observer_->OnPluginCrashed(*this, crash);
  }
}

}  // namespace views_shell
