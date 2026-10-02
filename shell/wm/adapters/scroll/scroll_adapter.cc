// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/wm/adapters/scroll/scroll_adapter.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/process/process_handle.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"

namespace views_shell::scroll {
namespace {

// True when `range` holds `value`.
template <typename Range, typename T>
bool Contains(const Range& range, const T& value) {
  return std::ranges::find(range, value) != std::ranges::end(range);
}

// docs/compositor-adapters.md, "Capabilities": the "yes" rows per variant.
constexpr const char* kSwayCapabilities[] = {
    "config.include-slot", "config.reload",
    "bindings.events",     "outputs.list",
    "scratchpad.toggle",   "session.exit",
    "windows.focus",       "windows.fullscreen-state",
    "windows.list",        "windows.move-to-workspace",
    "windows.urgency",     "workspaces.focus",
    "workspaces.list",     "workspaces.rename",
};
constexpr const char* kScrollOnlyCapabilities[] = {
    "bindings.list",   "overview.toggle", "scroll.jump",   "scroll.lua",
    "scroll.overview", "scroll.scroller", "scroll.spaces", "scroll.trails",
};

// The "with ask Hn" rows: declared only when GET_VERSION's `features` names
// them (hook H10).
struct ConditionalCapability {
  const char* capability;
  const char* hook;
  bool scroll;
  bool sway;
};
constexpr ConditionalCapability kConditionalCapabilities[] = {
    {"overview.events", "H2", true, false},
    {"windows.geometry-events", "H3", true, false},
    {"bindings.gesture-events", "H4", true, true},
};

constexpr std::string_view kBindingPrefix = "nop views-shell ";

std::string IdString(const base::DictValue& node) {
  std::optional<int> id = node.FindInt("id");
  return id ? base::NumberToString(*id) : std::string();
}

std::string StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

bool IsDigits(std::string_view s) {
  return !s.empty() &&
         std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

// A double-quoted command argument; the compositor strips the quotes and the
// escapes (sway's split_args, strip_quotes and unescape_string).
std::string Quote(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  return out;
}

bool IsView(const base::DictValue& node) {
  const std::string* type = node.FindString("type");
  if (!type || (*type != "con" && *type != "floating_con")) {
    return false;
  }
  const base::ListValue* nodes = node.FindList("nodes");
  const base::ListValue* floating = node.FindList("floating_nodes");
  if ((nodes && !nodes->empty()) || (floating && !floating->empty())) {
    return false;
  }
  return node.FindInt("pid").has_value() || node.FindString("app_id") ||
         node.FindString("shell") || node.FindDict("window_properties");
}

struct WalkContext {
  std::string workspace;  // id; empty in the scratchpad or above workspaces
  bool in_workspace =
      false;  // below a workspace node (the scratchpad included)
  std::optional<int> column;
};

void CollectWindows(const base::DictValue& node,
                    WalkContext context,
                    bool scroll_columns,
                    std::vector<WmWindow>& windows) {
  const std::string type = StringOr(node, "type");
  if (type == "workspace") {
    context.in_workspace = true;
    context.workspace = StringOr(node, "name") == "__i3_scratch"
                            ? std::string()
                            : IdString(node);
    context.column.reset();
  }
  if (context.in_workspace && IsView(node)) {
    WmWindow window;
    window.id = IdString(node);
    window.app_id = StringOr(node, "app_id");
    if (window.app_id.empty()) {
      if (const base::DictValue* props = node.FindDict("window_properties")) {
        window.app_id = StringOr(*props, "class");
      }
    }
    window.title = StringOr(node, "name");
    window.workspace = context.workspace;
    window.focused = node.FindBool("focused").value_or(false);
    window.fullscreen = node.FindInt("fullscreen_mode").value_or(0) > 0;
    window.urgent = node.FindBool("urgent").value_or(false);
    window.column = context.column;
    windows.push_back(std::move(window));
    return;
  }
  if (const base::ListValue* nodes = node.FindList("nodes")) {
    int index = 0;
    for (const base::Value& child : *nodes) {
      if (!child.is_dict()) {
        continue;
      }
      WalkContext child_context = context;
      if (type == "workspace" && scroll_columns && !context.workspace.empty()) {
        child_context.column = index;
      }
      CollectWindows(child.GetDict(), child_context, scroll_columns, windows);
      ++index;
    }
  }
  if (const base::ListValue* floating = node.FindList("floating_nodes")) {
    for (const base::Value& child : *floating) {
      if (child.is_dict()) {
        WalkContext child_context = context;
        child_context.column.reset();
        CollectWindows(child.GetDict(), child_context, scroll_columns, windows);
      }
    }
  }
}

// Depth first, each node's children in its `focus` order first, then the
// rest in tree order: the compositor's focus stacks flattened into an MRU.
void CollectFocusOrder(const base::DictValue& node,
                       std::vector<std::string>& order) {
  if (IsView(node)) {
    order.push_back(IdString(node));
    return;
  }
  std::vector<const base::DictValue*> children;
  for (const char* key : {"nodes", "floating_nodes"}) {
    if (const base::ListValue* list = node.FindList(key)) {
      for (const base::Value& child : *list) {
        if (child.is_dict()) {
          children.push_back(&child.GetDict());
        }
      }
    }
  }
  std::vector<const base::DictValue*> ordered;
  if (const base::ListValue* focus = node.FindList("focus")) {
    for (const base::Value& id : *focus) {
      if (!id.is_int()) {
        continue;
      }
      for (const base::DictValue* child : children) {
        if (child->FindInt("id") == id.GetInt() && !Contains(ordered, child)) {
          ordered.push_back(child);
        }
      }
    }
  }
  for (const base::DictValue* child : children) {
    if (!Contains(ordered, child)) {
      ordered.push_back(child);
    }
  }
  for (const base::DictValue* child : ordered) {
    CollectFocusOrder(*child, order);
  }
}

bool ReplySucceeded(const base::Value& reply, std::string* error) {
  if (!reply.is_list() || reply.GetList().empty()) {
    *error = "malformed RUN_COMMAND reply";
    return false;
  }
  bool ok = true;
  for (const base::Value& item : reply.GetList()) {
    if (!item.is_dict() ||
        !item.GetDict().FindBool("success").value_or(false)) {
      ok = false;
      if (item.is_dict() && error->empty()) {
        *error = StringOr(item.GetDict(), "error");
      }
    }
  }
  return ok;
}

}  // namespace

const std::vector<std::string>& AdapterSubscriptions() {
  static const base::NoDestructor<std::vector<std::string>> kSubscriptions(
      std::vector<std::string>{"workspace", "window", "output", "binding",
                               "shutdown", "tick"});
  return *kSubscriptions;
}

CapabilitySet ProbeCapabilities(const base::DictValue& version,
                                std::string* name) {
  const std::string* variant = version.FindString("variant");
  const bool scroll = variant && *variant == "scroll";
  *name = scroll ? "scroll" : "sway";

  std::vector<Capability> capabilities;
  for (const char* capability : kSwayCapabilities) {
    capabilities.emplace_back(capability);
  }
  if (scroll) {
    for (const char* capability : kScrollOnlyCapabilities) {
      capabilities.emplace_back(capability);
    }
  }
  if (const base::ListValue* features = version.FindList("features")) {
    for (const ConditionalCapability& row : kConditionalCapabilities) {
      if (!(scroll ? row.scroll : row.sway)) {
        continue;
      }
      for (const base::Value& feature : *features) {
        if (feature.is_string() &&
            (feature.GetString() == row.capability ||
             base::EqualsCaseInsensitiveASCII(feature.GetString(), row.hook))) {
          capabilities.emplace_back(row.capability);
          break;
        }
      }
    }
  }
  return CapabilitySet(std::move(capabilities));
}

WmSnapshot BuildSnapshot(const base::Value& outputs,
                         const base::Value& workspaces,
                         const base::Value& tree,
                         bool scroll_columns) {
  WmSnapshot snapshot;
  if (outputs.is_list()) {
    for (const base::Value& item : outputs.GetList()) {
      if (!item.is_dict()) {
        continue;
      }
      const base::DictValue& dict = item.GetDict();
      if (!dict.FindBool("active").value_or(true)) {
        continue;  // a disabled output holds no workspace
      }
      WmOutput output;
      output.id = StringOr(dict, "name");
      output.name = std::string(base::TrimWhitespaceASCII(
          base::StrCat({StringOr(dict, "make"), " ", StringOr(dict, "model")}),
          base::TRIM_ALL));
      const base::DictValue* mode = dict.FindDict("current_mode");
      if (!mode) {
        mode = dict.FindDict("rect");
      }
      if (mode) {
        output.width_px = mode->FindInt("width").value_or(0);
        output.height_px = mode->FindInt("height").value_or(0);
      }
      output.scale = dict.FindDouble("scale").value_or(1.0);
      output.focused = dict.FindBool("focused").value_or(false);
      snapshot.outputs.push_back(std::move(output));
    }
  }
  if (workspaces.is_list()) {
    std::map<std::string, int> per_output;
    for (const base::Value& item : workspaces.GetList()) {
      if (!item.is_dict()) {
        continue;
      }
      const base::DictValue& dict = item.GetDict();
      WmWorkspace workspace;
      workspace.id = IdString(dict);
      workspace.name = StringOr(dict, "name");
      workspace.output = StringOr(dict, "output");
      workspace.index = per_output[workspace.output]++;
      workspace.focused = dict.FindBool("focused").value_or(false);
      workspace.active = dict.FindBool("visible").value_or(false);
      workspace.urgent = dict.FindBool("urgent").value_or(false);
      snapshot.workspaces.push_back(std::move(workspace));
    }
  }
  if (tree.is_dict()) {
    CollectWindows(tree.GetDict(), WalkContext(), scroll_columns,
                   snapshot.windows);
    CollectFocusOrder(tree.GetDict(), snapshot.mru);
  }
  return snapshot;
}

std::optional<std::string> CommandText(const WmCommand& command,
                                       const WmSnapshot& snapshot) {
  // A workspace argument by id (resolved to its name) or by name.
  auto workspace_name = [&]() -> std::optional<std::string> {
    if (!command.workspace.empty()) {
      const WmWorkspace* workspace = snapshot.FindWorkspace(command.workspace);
      if (!workspace || workspace->name.empty()) {
        return std::nullopt;
      }
      return workspace->name;
    }
    if (command.name.empty()) {
      return std::nullopt;
    }
    return command.name;
  };
  auto window_criteria = [&]() -> std::optional<std::string> {
    if (!IsDigits(command.window) || !snapshot.FindWindow(command.window)) {
      return std::nullopt;
    }
    return base::StrCat({"[con_id=", command.window, "]"});
  };

  switch (command.kind) {
    case WmCommand::Kind::kFocusWorkspace: {
      std::optional<std::string> name = workspace_name();
      if (!name) {
        return std::nullopt;
      }
      // --no-auto-back-and-forth: with workspace_auto_back_and_forth in the
      // user's config, focusing the focused workspace would bounce away.
      return base::StrCat(
          {"workspace --no-auto-back-and-forth ", Quote(*name)});
    }
    case WmCommand::Kind::kFocusWindow: {
      std::optional<std::string> criteria = window_criteria();
      if (!criteria) {
        return std::nullopt;
      }
      return base::StrCat({*criteria, " focus"});
    }
    case WmCommand::Kind::kRenameWorkspace: {
      const WmWorkspace* workspace = snapshot.FindWorkspace(command.workspace);
      if (!workspace || workspace->name.empty() || command.name.empty()) {
        return std::nullopt;
      }
      return base::StrCat({"rename workspace ", Quote(workspace->name), " to ",
                           Quote(command.name)});
    }
    case WmCommand::Kind::kMoveWindowToWorkspace: {
      std::optional<std::string> criteria = window_criteria();
      std::optional<std::string> name = workspace_name();
      if (!criteria || !name) {
        return std::nullopt;
      }
      return base::StrCat(
          {*criteria, " move container to workspace ", Quote(*name)});
    }
    case WmCommand::Kind::kToggleScratchpad:
      return "scratchpad show";
    case WmCommand::Kind::kSessionExit:
      return "exit";
    case WmCommand::Kind::kReloadConfig:
      return "reload";
  }
  return std::nullopt;
}

ScrollAdapter::Options::Options() = default;
ScrollAdapter::Options::Options(const Options&) = default;
ScrollAdapter::Options& ScrollAdapter::Options::operator=(const Options&) =
    default;
ScrollAdapter::Options::~Options() = default;

ScrollAdapter::PendingCommand::PendingCommand() = default;
ScrollAdapter::PendingCommand::PendingCommand(PendingCommand&&) = default;
ScrollAdapter::PendingCommand& ScrollAdapter::PendingCommand::operator=(
    PendingCommand&&) = default;
ScrollAdapter::PendingCommand::~PendingCommand() = default;

ScrollAdapter::ScrollAdapter(Options options)
    : options_(std::move(options)), backoff_(options_.initial_backoff) {}

ScrollAdapter::~ScrollAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Dropped, not run: the delegate may be going away too.
  pending_.clear();
}

std::string_view ScrollAdapter::name() const {
  return name_;
}

const CapabilitySet& ScrollAdapter::capabilities() const {
  return capabilities_;
}

void ScrollAdapter::Start(Delegate* delegate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(delegate);
  CHECK(!delegate_) << "ScrollAdapter::Start() called twice";
  delegate_ = delegate;
  Connect();
}

void ScrollAdapter::Connect() {
  client_.reset();
  ResetConnectionState();
  ++generation_;
  client_ = std::make_unique<ScrollIpcClient>(options_.socket_path,
                                              AdapterSubscriptions());
  client_->Start(this);
}

void ScrollAdapter::ResetConnectionState() {
  transport_up_ = false;
  probed_ = false;
  has_snapshot_ = false;
  version_ = base::DictValue();
  capabilities_ = CapabilitySet();
  event_seq_ = 0;
  delivered_seq_ = 0;
  refresh_in_flight_ = false;
  refresh_dirty_ = false;
  refresh_seq_ = 0;
  refresh_outputs_.reset();
  refresh_workspaces_.reset();
  mru_.clear();
  current_ = WmSnapshot();
}

void ScrollAdapter::OnConnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  transport_up_ = true;
  client_->Request(kIpcGetVersion, std::string(),
                   base::BindOnce(&ScrollAdapter::OnVersion,
                                  weak_factory_.GetWeakPtr(), generation_));
}

void ScrollAdapter::OnVersion(uint64_t generation,
                              std::optional<base::Value> value) {
  if (generation != generation_ || !value) {
    return;  // a lost connection; OnDisconnected follows
  }
  if (value->is_dict()) {
    version_ = std::move(*value).TakeDict();
  }
  capabilities_ = ProbeCapabilities(version_, &name_);
  probed_ = true;
  VLOG(1) << "scroll adapter: " << name_ << " "
          << StringOr(version_, "human_readable") << ", "
          << capabilities_.size() << " capabilities";
  SendRefresh();
}

void ScrollAdapter::RequestRefresh() {
  if (!probed_) {
    return;  // the first refresh, after GET_VERSION, covers this event
  }
  if (refresh_in_flight_) {
    refresh_dirty_ = true;
    return;
  }
  SendRefresh();
}

void ScrollAdapter::SendRefresh() {
  refresh_in_flight_ = true;
  refresh_dirty_ = false;
  refresh_seq_ = event_seq_;
  refresh_outputs_.reset();
  refresh_workspaces_.reset();
  for (uint32_t type : {kIpcGetOutputs, kIpcGetWorkspaces, kIpcGetTree}) {
    client_->Request(
        type, std::string(),
        base::BindOnce(&ScrollAdapter::OnRefreshPart,
                       weak_factory_.GetWeakPtr(), generation_, type));
  }
}

void ScrollAdapter::OnRefreshPart(uint64_t generation,
                                  uint32_t type,
                                  std::optional<base::Value> value) {
  if (generation != generation_ || !value) {
    return;
  }
  if (type == kIpcGetOutputs) {
    refresh_outputs_ = std::move(value);
    return;
  }
  if (type == kIpcGetWorkspaces) {
    refresh_workspaces_ = std::move(value);
    return;
  }
  // GET_TREE is answered last: the three replies come in request order.
  if (!refresh_outputs_ || !refresh_workspaces_) {
    LOG(ERROR) << "scroll adapter: GET_TREE answered before its siblings";
    return;
  }
  WmSnapshot snapshot = BuildSnapshot(*refresh_outputs_, *refresh_workspaces_,
                                      *value, name_ == "scroll");
  refresh_outputs_.reset();
  refresh_workspaces_.reset();
  refresh_in_flight_ = false;
  const uint64_t covered = refresh_seq_;

  // MRU: the focus events seen so far, newest first, then the rest of the
  // compositor's focus-stack order.
  std::vector<std::string> mru;
  for (const std::string& id : mru_) {
    if (snapshot.FindWindow(id) && !Contains(mru, id)) {
      mru.push_back(id);
    }
  }
  for (const std::string& id : snapshot.mru) {
    if (!Contains(mru, id)) {
      mru.push_back(id);
    }
  }
  mru_ = mru;
  snapshot.mru = std::move(mru);

  if (refresh_dirty_) {
    SendRefresh();
  }
  delivered_seq_ = covered;
  DeliverSnapshot(std::move(snapshot));
}

void ScrollAdapter::DeliverSnapshot(WmSnapshot snapshot) {
  has_snapshot_ = true;
  backoff_ = options_.initial_backoff;
  current_ = snapshot;
  const uint64_t generation = generation_;
  base::WeakPtr<ScrollAdapter> self = weak_factory_.GetWeakPtr();
  delegate_->OnSnapshot(std::move(snapshot));
  if (self && generation == generation_) {
    CheckBarriers();
  }
}

void ScrollAdapter::OnEvent(uint32_t type, base::Value payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!payload.is_dict()) {
    return;
  }
  const base::DictValue& dict = payload.GetDict();
  const std::string change = StringOr(dict, "change");
  switch (type) {
    case kIpcWindowEvent:
      if (change == "focus") {
        if (const base::DictValue* container = dict.FindDict("container")) {
          const std::string id = IdString(*container);
          std::erase(mru_, id);
          mru_.insert(mru_.begin(), id);
        }
      }
      ++event_seq_;
      RequestRefresh();
      return;
    case kIpcWorkspaceEvent:
      ++event_seq_;
      RequestRefresh();
      if (change == "reload" && probed_) {
        delegate_->OnConfigReloaded(true, std::string_view());
      }
      return;
    case kIpcOutputEvent:
      ++event_seq_;
      RequestRefresh();
      return;
    case kIpcBindingEvent: {
      const base::DictValue* binding = dict.FindDict("binding");
      const std::string command =
          binding ? StringOr(*binding, "command") : std::string();
      if (base::StartsWith(command, kBindingPrefix)) {
        delegate_->OnBinding(
            std::string_view(command).substr(kBindingPrefix.size()));
      }
      return;
    }
    case kIpcShutdownEvent:
      VLOG(1) << "scroll adapter: compositor shutdown (" << change << ")";
      // `exit` is echoed by this event; the socket closes next.
      for (auto& [serial, command] : pending_) {
        if (command.kind == WmCommand::Kind::kSessionExit) {
          command.state = PendingCommand::State::kAwaitSnapshot;
          command.barrier_seq = 0;
        }
      }
      CheckBarriers();
      return;
    case kIpcTickEvent:
      OnTick(dict);
      return;
    default:
      return;
  }
}

void ScrollAdapter::OnTick(const base::DictValue& payload) {
  if (payload.FindBool("first").value_or(false)) {
    return;  // sent once on SUBSCRIBE
  }
  const std::string tick = StringOr(payload, "payload");
  for (auto& [serial, command] : pending_) {
    if (command.state == PendingCommand::State::kAwaitTick &&
        command.tick == tick) {
      // Every event the command caused came before this tick.
      command.state = PendingCommand::State::kAwaitSnapshot;
      command.barrier_seq = event_seq_;
    }
  }
  CheckBarriers();
}

void ScrollAdapter::CheckBarriers() {
  std::vector<uint64_t> landed;
  for (const auto& [serial, command] : pending_) {
    if (command.state == PendingCommand::State::kAwaitSnapshot &&
        (command.barrier_seq <= delivered_seq_ &&
         (has_snapshot_ || command.kind == WmCommand::Kind::kSessionExit))) {
      landed.push_back(serial);
    }
  }
  for (uint64_t serial : landed) {
    Finish(serial, base::ok());
  }
}

void ScrollAdapter::OnDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool was_up = transport_up_;
  LOG_IF(WARNING, was_up) << "scroll adapter: lost the compositor connection";
  FailAllCommands(WmCommandError::kNotConnected);
  ResetConnectionState();
  // The client is destroyed by the reconnect, not here: this call came from it.
  reconnect_timer_.Start(
      FROM_HERE, backoff_,
      base::BindOnce(&ScrollAdapter::Connect, base::Unretained(this)));
  backoff_ = std::min(backoff_ * 2, options_.max_backoff);
  if (was_up) {
    delegate_->OnDisconnected();
  }
}

void ScrollAdapter::Send(const WmCommand& command, CommandDone done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto fail = [&](WmCommandError error) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), base::unexpected(error)));
  };
  if (!client_ || !has_snapshot_) {
    fail(WmCommandError::kNotConnected);
    return;
  }
  if (!capabilities_.contains(RequiredCapability(command.kind))) {
    fail(WmCommandError::kCapabilityMissing);
    return;
  }
  std::optional<std::string> text = CommandText(command, current_);
  if (!text) {
    fail(WmCommandError::kInvalidArgument);
    return;
  }
  const uint64_t serial = next_serial_++;
  PendingCommand pending;
  pending.kind = command.kind;
  pending.tick = base::StrCat({"views-shell-",
                               base::NumberToString(base::GetCurrentProcId()),
                               "-", base::NumberToString(serial)});
  pending.done = std::move(done);
  pending_.emplace(serial, std::move(pending));
  VLOG(1) << "scroll adapter: RUN_COMMAND " << *text;
  client_->Request(
      kIpcRunCommand, *text,
      base::BindOnce(&ScrollAdapter::OnCommandReply, weak_factory_.GetWeakPtr(),
                     generation_, serial));
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ScrollAdapter::OnCommandTimeout,
                     weak_factory_.GetWeakPtr(), serial),
      options_.command_timeout);
}

void ScrollAdapter::OnCommandReply(uint64_t generation,
                                   uint64_t serial,
                                   std::optional<base::Value> value) {
  auto it = pending_.find(serial);
  if (generation != generation_ || it == pending_.end() || !value) {
    return;  // failed with the connection already
  }
  std::string error;
  if (!ReplySucceeded(*value, &error)) {
    LOG(WARNING) << "scroll adapter: command refused: " << error;
    const bool reload = it->second.kind == WmCommand::Kind::kReloadConfig;
    Finish(serial, base::unexpected(WmCommandError::kRejected));
    if (reload) {
      delegate_->OnConfigReloaded(false, error);
    }
    return;
  }
  if (it->second.state != PendingCommand::State::kAwaitReply) {
    return;  // exit: the shutdown event got here first
  }
  it->second.state = PendingCommand::State::kAwaitTick;
  client_->Request(kIpcSendTick, it->second.tick, base::DoNothing());
}

void ScrollAdapter::OnCommandTimeout(uint64_t serial) {
  if (pending_.contains(serial)) {
    LOG(WARNING) << "scroll adapter: no echo for command " << serial;
    Finish(serial, base::unexpected(WmCommandError::kNoEcho));
  }
}

void ScrollAdapter::Finish(uint64_t serial,
                           base::expected<void, WmCommandError> result) {
  auto it = pending_.find(serial);
  if (it == pending_.end()) {
    return;
  }
  CommandDone done = std::move(it->second.done);
  pending_.erase(it);
  // Posted: the snapshot that carried the echo has reached the delegate and
  // its observers before the caller hears about it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(done), result));
}

void ScrollAdapter::FailAllCommands(WmCommandError error) {
  std::vector<uint64_t> serials;
  for (const auto& [serial, command] : pending_) {
    serials.push_back(serial);
  }
  for (uint64_t serial : serials) {
    const bool exited =
        pending_[serial].kind == WmCommand::Kind::kSessionExit &&
        pending_[serial].state == PendingCommand::State::kAwaitSnapshot;
    Finish(serial, exited ? base::expected<void, WmCommandError>(base::ok())
                          : base::unexpected(error));
  }
}

}  // namespace views_shell::scroll
