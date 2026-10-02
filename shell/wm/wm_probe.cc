// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// wm_probe: the compositor adapters' run gate. A console program (no Wayland,
// no Views) that runs the real adapter and WmModel against the compositor in
// its environment and prints what it sees.
//
//   wm_probe --dump              print the first snapshot as one JSON object
//                                (compositor, version, capabilities, outputs,
//                                workspaces, windows, mru) and exit 0
//   wm_probe --switch <name>     focus workspace <name> through WmModel; when
//                                the echo has been delivered, print
//                                "ECHO workspace <name>" and exit 0
//   wm_probe --watch <seconds>   print every change the model reports for
//                                that long, then exit 0
//   --timeout-seconds <n>        give up after n seconds (default 20) and exit
//   1
//   --socket <path>              use this socket instead of resolving one
//
// Every mode first prints "CONNECTED <compositor> <version>" and the
// capability set the probe narrowed at connect. Diagnostics go to stderr.

#include <unistd.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "views_shell/wm/adapters/scroll/scroll_adapter.h"
#include "views_shell/wm/wm_model.h"

namespace views_shell {
namespace {

constexpr char kUsage[] =
    "usage: wm_probe (--dump | --switch <name> | --watch <seconds>)\n"
    "                [--timeout-seconds <n>] [--socket <path>]\n";

void Print(std::string_view line) {
  base::WriteFileDescriptor(STDOUT_FILENO, base::StrCat({line, "\n"}));
}

// Status lines go to stderr, so `--dump` leaves nothing but JSON on stdout.
void Status(std::string_view line) {
  base::WriteFileDescriptor(STDERR_FILENO, base::StrCat({line, "\n"}));
}

// "--name value" and "--name=value" both work; base::CommandLine alone only
// takes the second form. Returns nullopt when --name is absent.
std::optional<std::string> Flag(const base::CommandLine::StringVector& argv,
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

base::ListValue CapabilityList(const CapabilitySet& capabilities) {
  base::ListValue list;
  for (const Capability& capability : capabilities) {
    list.Append(capability);
  }
  return list;
}

base::DictValue SnapshotToDict(const WmSnapshot& snapshot) {
  base::ListValue outputs;
  for (const WmOutput& output : snapshot.outputs) {
    outputs.Append(base::DictValue()
                       .Set("id", output.id)
                       .Set("name", output.name)
                       .Set("width", output.width_px)
                       .Set("height", output.height_px)
                       .Set("scale", output.scale)
                       .Set("focused", output.focused));
  }
  base::ListValue workspaces;
  for (const WmWorkspace& workspace : snapshot.workspaces) {
    workspaces.Append(base::DictValue()
                          .Set("id", workspace.id)
                          .Set("name", workspace.name)
                          .Set("index", workspace.index)
                          .Set("output", workspace.output)
                          .Set("focused", workspace.focused)
                          .Set("active", workspace.active)
                          .Set("urgent", workspace.urgent));
  }
  base::ListValue windows;
  for (const WmWindow& window : snapshot.windows) {
    base::DictValue dict = base::DictValue()
                               .Set("id", window.id)
                               .Set("app_id", window.app_id)
                               .Set("title", window.title)
                               .Set("workspace", window.workspace)
                               .Set("focused", window.focused)
                               .Set("fullscreen", window.fullscreen)
                               .Set("urgent", window.urgent);
    if (window.column) {
      dict.Set("column", *window.column);
    }
    windows.Append(std::move(dict));
  }
  base::ListValue mru;
  for (const std::string& id : snapshot.mru) {
    mru.Append(id);
  }
  return base::DictValue()
      .Set("outputs", std::move(outputs))
      .Set("workspaces", std::move(workspaces))
      .Set("windows", std::move(windows))
      .Set("mru", std::move(mru));
}

class Probe : public WmModel::Observer {
 public:
  enum class Mode { kDump, kSwitch, kWatch };

  Probe(Mode mode,
        std::string target,
        scroll::ScrollAdapter* adapter,
        WmModel* model,
        base::OnceClosure quit)
      : mode_(mode),
        target_(std::move(target)),
        adapter_(adapter),
        model_(model),
        quit_(std::move(quit)) {}
  ~Probe() override = default;

  int exit_code() const { return exit_code_; }
  bool connected() const { return connected_printed_; }

  void Finish(int code) {
    if (quit_) {
      exit_code_ = code;
      std::move(quit_).Run();
    }
  }

  // WmModel::Observer:
  void OnSnapshotApplied() override {
    const WmSnapshot& snapshot = model_->snapshot();
    if (!connected_printed_) {
      connected_printed_ = true;
      const std::string* version =
          adapter_->version().FindString("human_readable");
      Status(base::StrCat({"CONNECTED ", adapter_->name(), " ",
                           version ? *version : std::string("unknown")}));
      std::string capabilities;
      for (const Capability& capability : adapter_->capabilities()) {
        base::StrAppend(&capabilities, {" ", capability});
      }
      Status(base::StrCat({"CAPABILITIES", capabilities}));
      Start();
      return;
    }
    if (mode_ == Mode::kWatch) {
      const WmWorkspace* workspace = snapshot.FocusedWorkspace();
      Print(base::StrCat(
          {"SNAPSHOT outputs=", base::NumberToString(snapshot.outputs.size()),
           " workspaces=", base::NumberToString(snapshot.workspaces.size()),
           " windows=", base::NumberToString(snapshot.windows.size()),
           " focused-workspace=", workspace ? workspace->name : "-"}));
    }
  }
  void OnChildAdded(const std::string& parent,
                    const std::string& id,
                    int index) override {
    Watch({"ADDED parent=", Show(parent), " id=", id,
           " index=", base::NumberToString(index)});
  }
  void OnChildRemoved(const std::string& parent,
                      const std::string& id) override {
    Watch({"REMOVED parent=", Show(parent), " id=", id});
  }
  void OnChildMoved(const std::string& old_parent,
                    const std::string& new_parent,
                    const std::string& id,
                    int new_index) override {
    Watch({"MOVED id=", id, " from=", Show(old_parent), " to=",
           Show(new_parent), " index=", base::NumberToString(new_index)});
  }
  void OnItemChanged(const std::string& id) override {
    Watch({"CHANGED id=", id});
  }
  void OnFocusChanged(const std::string& workspace,
                      const std::string& window) override {
    Watch({"FOCUS workspace=", Show(workspace), " window=", Show(window)});
  }
  void OnMruChanged() override {
    Watch({"MRU ", base::JoinString(model_->snapshot().mru, ",")});
  }
  void OnBinding(std::string_view payload) override {
    Watch({"BINDING ", payload});
  }
  void OnConfigReloaded(bool ok, std::string_view error) override {
    Watch({"RELOAD ", ok ? "ok" : "failed ", error});
  }
  void OnDisconnected() override { Print("DISCONNECTED"); }

 private:
  static std::string Show(const std::string& id) {
    return id.empty() ? "-" : id;
  }

  void Watch(std::initializer_list<std::string_view> parts) {
    if (mode_ == Mode::kWatch && connected_printed_) {
      Print(base::StrCat(parts));
    }
  }

  void Start() {
    switch (mode_) {
      case Mode::kDump: {
        base::DictValue dump = SnapshotToDict(model_->snapshot());
        dump.Set("compositor", std::string(adapter_->name()));
        const std::string* version =
            adapter_->version().FindString("human_readable");
        dump.Set("version", version ? *version : std::string());
        dump.Set("capabilities", CapabilityList(adapter_->capabilities()));
        Print(base::WriteJsonWithOptions(dump,
                                         base::JSONWriter::OPTIONS_PRETTY_PRINT)
                  .value_or("{}"));
        Finish(0);
        return;
      }
      case Mode::kSwitch: {
        auto done =
            base::BindOnce(&Probe::OnSwitchDone, base::Unretained(this));
        const WmWorkspace* existing = nullptr;
        for (const WmWorkspace& workspace : model_->snapshot().workspaces) {
          if (workspace.name == target_) {
            existing = &workspace;
          }
        }
        Print(base::StrCat({"SWITCH workspace ", target_,
                            existing ? " (existing id " : " (by name",
                            existing ? existing->id : std::string(), ")"}));
        if (existing) {
          model_->FocusWorkspace(existing->id, std::move(done));
        } else {
          model_->FocusWorkspaceByName(target_, std::move(done));
        }
        return;
      }
      case Mode::kWatch:
        Print("WATCHING");
        return;
    }
  }

  void OnSwitchDone(base::expected<void, WmCommandError> result) {
    if (!result.has_value()) {
      Print(base::StrCat({"FAILED workspace ", target_, ": ",
                          WmCommandErrorName(result.error())}));
      Finish(1);
      return;
    }
    // The echo: the snapshot the barrier waited for shows the focus.
    const WmWorkspace* focused = model_->snapshot().FocusedWorkspace();
    if (focused && focused->name == target_) {
      Print(base::StrCat({"ECHO workspace ", target_}));
      Finish(0);
      return;
    }
    Print(base::StrCat({"NO-ECHO workspace ", target_, ": focused is ",
                        focused ? focused->name : std::string("none")}));
    Finish(1);
  }

  const Mode mode_;
  const std::string target_;
  const raw_ptr<scroll::ScrollAdapter> adapter_;
  const raw_ptr<WmModel> model_;
  base::OnceClosure quit_;
  bool connected_printed_ = false;
  int exit_code_ = 1;
};

int Main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  logging::InitLogging(settings);
  base::FeatureList::InitInstance(std::string(), std::string());

  const base::CommandLine::StringVector& argv =
      base::CommandLine::ForCurrentProcess()->argv();
  std::optional<Probe::Mode> mode;
  std::string target;
  int watch_seconds = 0;
  if (Flag(argv, "dump")) {
    mode = Probe::Mode::kDump;
  } else if (std::optional<std::string> name = Flag(argv, "switch")) {
    mode = Probe::Mode::kSwitch;
    target = *name;
  } else if (std::optional<std::string> seconds = Flag(argv, "watch")) {
    mode = Probe::Mode::kWatch;
    if (!base::StringToInt(*seconds, &watch_seconds) || watch_seconds <= 0) {
      mode.reset();
    }
  }
  int timeout_seconds = 20;
  if (std::optional<std::string> timeout = Flag(argv, "timeout-seconds");
      timeout && (!base::StringToInt(*timeout, &timeout_seconds) ||
                  timeout_seconds <= 0)) {
    mode.reset();
  }
  if (!mode || (*mode == Probe::Mode::kSwitch && target.empty())) {
    base::WriteFileDescriptor(STDERR_FILENO, kUsage);
    return 2;
  }

  base::SingleThreadTaskExecutor executor;
  base::RunLoop run_loop;

  scroll::ScrollAdapter::Options options;
  options.socket_path = Flag(argv, "socket").value_or(std::string());
  scroll::ScrollAdapter adapter(options);
  WmModel model(&adapter);
  Probe probe(*mode, target, &adapter, &model, run_loop.QuitClosure());
  model.AddObserver(&probe);

  // --watch ends well after its period; every other mode fails at the timeout.
  const bool watching = *mode == Probe::Mode::kWatch;
  executor.task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](Probe* probe, bool watching, int seconds) {
            const bool ok = watching && probe->connected();
            if (!ok) {
              Print(base::StrCat(
                  {"TIMEOUT after ", base::NumberToString(seconds), " s"}));
            }
            probe->Finish(ok ? 0 : 1);
          },
          base::Unretained(&probe), watching,
          watching ? watch_seconds : timeout_seconds),
      base::Seconds(watching ? watch_seconds : timeout_seconds));

  adapter.Start(&model);
  run_loop.Run();
  model.RemoveObserver(&probe);
  return probe.exit_code();
}

}  // namespace
}  // namespace views_shell

int main(int argc, char** argv) {
  return views_shell::Main(argc, argv);
}
