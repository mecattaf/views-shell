# shell/plugins: the plugin host

Layer 6 of [`docs/architecture.md`](../../docs/architecture.md) §6: the plugin
registry, the permissions broker, and the T1 and T2 tiers, as a library with
no Views, no Wayland and no renderer in it. Trees and snapshots leave through
two interfaces that the assembly binds to the ui_tree renderer.

| File | What |
|---|---|
| `plugin_manifest.{h,cc}` | `views-shell-plugin.json` → `PluginManifest`. Every rule of `schemas/views-shell-plugin.schema.json`, written by hand (Chromium has no JSON Schema validator), keyword by keyword in the schema's order, with `jsonschema`'s own messages; then `tools/validate.py`'s cross-file checks. The first problem is the reason `tools/plugin-registry.py` prints after `REJECT`. Also declares `TreeSink`. |
| `registry_view.{h,cc}` | The registry view of `tools/plugin-registry.py` (its docstring is the contract), and a writer that prints Python's `json.dumps(indent=2, sort_keys=True, ensure_ascii=False)`, so views compare byte for byte with `tools/fixtures/registry/<id>.json`. |
| `plugin_registry.{h,cc}` | Discovers plugin directories under roots, validates, infers activation, gates on the adapter's `CapabilitySet` (rule R14: a missing required capability means not registered), records rejections and conflicts (duplicate id, shared key, shared CLI name, missing dependency). |
| `permissions_broker.{h,cc}` | Answers plugin requests: `notify` (`notifications`), `toast`, `exec` (`exec:<program>`, run with `base::LaunchProcess`, answered `{exitCode, stdout, stderr}`), `compositor/command` (`compositor:<capability>`, mapped to a typed `WmCommand` through a `WmCommandSink`). Undeclared: `-32001` "permission not declared". Also `AllowsCall` (`call:`) and `AllowsStateRead` (`state:read:`). |
| `json_rpc.{h,cc}` | JSON-RPC 2.0 messages and the line framing of `schemas/plugin-protocol.md`, tolerant of reads that end anywhere. |
| `process_plugin.{h,cc}` | A supervised T2 process: launch (scope or plain), `initialize`, the startup window, every host → plugin method, plugin requests through the broker, restart with doubling backoff, `Crash` reports, shutdown within the grace period. |
| `declarative_plugin.{h,cc}` | A T1 plugin: its ui files to the `TreeSink`, its handlers (`compositor`, `open`, `call`, `exec`) run on its behalf. |
| `plugin_host.{h,cc}` | Registry + broker + instances; activation (`onStartup` at `Start()`, the rest on first use); holds the latest snapshot and trees; reports to one `Delegate`. |
| `plugin_probe.cc` | The run gate: `plugin_probe --plugin <dir> [--invoke <command> --args <json>]` prints the registry view, every tree and snapshot for 5 s, the invoke's answer, `plugin alive:` and the launch path. |
| `testdata/` | Two test-only T2 plugins in `sh`: `exits-at-once` (crash and restart) and `wrong-protocol` (answers protocol 2). |

## The two binding points

```cpp
// plugin_manifest.h: what every plugin instance writes to. PluginHost
// implements it.
class TreeSink {
  virtual void OnTree(const PluginManifest&, const std::string& surface,
                      base::DictValue tree) = 0;
  virtual void OnSnapshot(const PluginManifest&, base::DictValue data) = 0;
  virtual void OnSourceSnapshot(const PluginManifest&,
                                const std::string& source,
                                base::Value data) = 0;
};

// plugin_host.h: what the assembly implements. The renderer takes OnTree and
// OnSnapshot, the notification service OnNotify and OnToast, the surface host
// OnCrash (a badge) and OnOpenControlPage (Chrome).
class PluginHost::Delegate {
  virtual void OnTree(const std::string& plugin, const std::string& surface,
                      const base::DictValue& tree,
                      const base::DictValue& snapshot) = 0;
  virtual void OnSnapshot(const std::string& plugin,
                          const base::DictValue& snapshot) = 0;
  virtual void OnSourceSnapshot(const std::string& source,
                                const base::Value& data) {}
  virtual void OnNotify(const std::string& plugin,
                        const PluginNotification&) = 0;
  virtual void OnToast(const std::string& plugin, const PluginToast&) = 0;
  virtual void OnCrash(const std::string& plugin,
                       const ProcessPlugin::Crash&) = 0;
  virtual void OnOpenControlPage(const std::string& plugin,
                                 const std::string& page) {}
};
```

`OnTree` arrives once the host holds both a tree and the snapshot it binds to
(for T1 the snapshot is `{}`: T1 trees bind to sources), and again whenever a
tree changes. Commands from every face go in through
`PluginHost::Invoke("<plugin-id>/<command>", args, source, callback)`; the
compositor goes in through `WmCommandSink`, which the running
`CompositorAdapter` satisfies (same `Send` signature).

## Threading and launch

`PluginHost` and `ProcessPlugin` live on one sequence that supports
`base::FileDescriptorWatcher` (a `MessagePumpType::IO` thread). Blocking work
(the launch-path probe, exec, waiting for an exit) runs on the thread pool.
`ProcessPlugin` ignores `SIGPIPE` once per process, so a write to a plugin that
just exited cannot end views-shell.

A T2 plugin runs in its own `systemd-run --user --scope` unit
(`views-shell-plugin-<id>-<pid>-<n>.scope`) when `systemd-run` is on `PATH` and
`systemd-run --user --scope --quiet --collect -- true` succeeds; otherwise it is
started with plain `base::LaunchProcess`. Inside `runtime-test` and the bench's
FHS build environment there is no reachable user manager, so tests take the
plain path. Every launch logs the path it took (`launched pid N via ...`), and
`ProcessPlugin::launch_path()` reports it.

## What the host settles beyond the protocol table

Recorded in [`schemas/plugin-protocol.md`](../../schemas/plugin-protocol.md)
under "Settled by the C++ host (w3b)": argument checking before a
`command/invoke` is sent, the answer-shape check, `-32000` for host-side
failures, the restart backoff, the protocol-mismatch rule (never restarted),
and what `exec` answers when the program cannot start or overruns its
deadline.

## Not done here

- Drawing: the ui_tree renderer binds `PluginHost::Delegate` in the assembly.
- `picker/query` and the `when`/`enablement` expression evaluator.
- `compositor/command` args beyond the typed `WmCommand` fields (for example
  music-scratchpad's `match: {app_id}`) are accepted and not used: `WmCommand`
  has no match field yet.
- T0 (builtin) plugins are registered; their C++ serves their commands.
- Settings store (`views-shell.json`) and enable-time permission display.
