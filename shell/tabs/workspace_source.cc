// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/tabs/workspace_source.h"

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"

namespace views_shell {
namespace {

class StaticWorkspaceSource : public WorkspaceSource {
 public:
  explicit StaticWorkspaceSource(std::vector<Workspace> workspaces)
      : workspaces_(std::move(workspaces)) {}

  std::string_view name() const override { return "static"; }

  void Fetch(FetchCallback callback) override {
    std::move(callback).Run(workspaces_);
  }

 private:
  const std::vector<Workspace> workspaces_;
};

// Blocking: runs on the thread pool.
std::vector<Workspace> RunNiriMsg() {
  base::CommandLine command(base::FilePath("niri"));
  command.AppendArg("msg");
  command.AppendArg("-j");
  command.AppendArg("workspaces");
  std::string output;
  if (!base::GetAppOutput(command, &output)) {
    LOG(ERROR) << "workspaces: `niri msg -j workspaces` failed";
    return {};
  }
  std::vector<Workspace> workspaces = ParseNiriWorkspaces(output);
  if (workspaces.empty()) {
    LOG(ERROR) << "workspaces: no workspaces in the niri reply";
  }
  return workspaces;
}

class NiriWorkspaceSource : public WorkspaceSource {
 public:
  std::string_view name() const override { return "niri"; }

  void Fetch(FetchCallback callback) override {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&RunNiriMsg), std::move(callback));
  }
};

// niri's ids are u64 and arrive as JSON numbers; base::Value keeps integers
// that fit an int as ints and the rest as doubles.
std::optional<std::string> NumberToString(const base::Value* value) {
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    return base::NumberToString(value->GetInt());
  }
  if (value->is_double()) {
    return base::NumberToString(static_cast<uint64_t>(value->GetDouble()));
  }
  return std::nullopt;
}

}  // namespace

std::unique_ptr<WorkspaceSource> CreateStaticWorkspaceSource(
    std::vector<Workspace> workspaces) {
  return std::make_unique<StaticWorkspaceSource>(std::move(workspaces));
}

std::vector<Workspace> DemoWorkspaces() {
  return {
      {.id = "1", .title = u"1  Web", .active = true},
      {.id = "2", .title = u"2  Code"},
      {.id = "3", .title = u"3  Chat"},
      {.id = "4", .title = u"4  Music"},
  };
}

std::unique_ptr<WorkspaceSource> CreateNiriWorkspaceSource() {
  return std::make_unique<NiriWorkspaceSource>();
}

std::vector<Workspace> ParseNiriWorkspaces(std::string_view json) {
  std::optional<base::ListValue> list =
      base::JSONReader::ReadList(json, base::JSON_PARSE_RFC);
  if (!list) {
    return {};
  }

  struct Entry {
    std::string output;
    int idx;
    Workspace workspace;
  };
  std::vector<Entry> entries;
  std::vector<std::string> outputs;
  for (const base::Value& item : *list) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      return {};
    }
    std::optional<std::string> id = NumberToString(dict->Find("id"));
    std::optional<int> idx = dict->FindInt("idx");
    if (!id || !idx) {
      return {};
    }
    const std::string* output = dict->FindString("output");
    const std::string* name = dict->FindString("name");
    Entry entry{.output = output ? *output : std::string(), .idx = *idx};
    entry.workspace.id = *id;
    entry.workspace.title = name ? base::UTF8ToUTF16(*name)
                                 : base::NumberToString16(*idx);
    entry.workspace.active = dict->FindBool("is_focused").value_or(false);
    if (std::ranges::find(outputs, entry.output) == outputs.end()) {
      outputs.push_back(entry.output);
    }
    entries.push_back(std::move(entry));
  }

  std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
    return std::tie(a.output, a.idx) < std::tie(b.output, b.idx);
  });

  std::vector<Workspace> workspaces;
  workspaces.reserve(entries.size());
  bool seen_active = false;
  for (Entry& entry : entries) {
    if (outputs.size() > 1 && !entry.output.empty()) {
      entry.workspace.title +=
          u" (" + base::UTF8ToUTF16(entry.output) + u")";
    }
    // At most one active workspace, whatever the reply says.
    entry.workspace.active = entry.workspace.active && !seen_active;
    seen_active |= entry.workspace.active;
    workspaces.push_back(std::move(entry.workspace));
  }
  return workspaces;
}

std::unique_ptr<WorkspaceSource> CreateWorkspaceSource(std::string_view name) {
  if (name.empty()) {
    name = base::Environment::Create()->HasVar("NIRI_SOCKET") ? "niri"
                                                              : "static";
  }
  if (name == "niri") {
    return CreateNiriWorkspaceSource();
  }
  if (name == "static") {
    return CreateStaticWorkspaceSource(DemoWorkspaces());
  }
  return nullptr;
}

}  // namespace views_shell
