# Provenance of lifted code

On 2026-10-01 earlier prototype code by the same author was **lifted byte for
byte** into this repository, following a 61-entry lift list in the private
scoping notes. No file was edited during the lift; each kept its original BSD-3
header and identifiers, so the lift commit diffs cleanly against its sources.

In chapter 2 (commit `0d30e89`) every lifted file that was neither built nor
applied was **deleted, not archived** (Tom's standing rule: delete superseded
experimental code, keep history). The tree had never compiled them, and their
planned re-cuts had landed: the layer-shell sources and protocol XML live inside
[`patches/views-shell-ozone-layer-shell.patch`](patches/views-shell-ozone-layer-shell.patch),
the popup and the build gate are live patches, the `//content` embedder was
provenance only (rule R24), and [`../tools/bench/`](../tools/bench/) replaces the
lifted wire scripts and `headless-eval.sh`. The table below stays as the record:
every deleted row says how to recover the file from git history, at the last
commit that held it (`93d691a`).

## Sources

| Short name | Location | Revision | What it proved |
|---|---|---|---|
| agency-mvp | agency-mvp (private repository) | `cd7cef3`, branch `rail/v1-readonly-mirror`, which contains `origin/main` `d669028` and the layer-window activation fix `a1b4fc0` | `ninja agency chrome` green at Chromium **150.0.7871.124** with layer-shell on; a Views bar on niri; a launcher with typed input on headless sway; the rail as a read-only mirror of live niri state |
| June 30 spike | the private devlog archive, the spike's architecture1 source snapshot | snapshot of 2026-06-30, Chromium **149.0.7827.102** | a content embedder without `//chrome` hosting Views on a layer surface with translucency, paint and keyboard (the four Views init gaps closed) |

No Chromium tree or binary from either survives; every lift was source and patches.

## The niri workspace switcher that worked

The switcher that actually switched niri workspaces (2026-06-27) was **web content**
on a layer surface, driven by `niri msg` subprocesses. Its JavaScript was never
lifted. Its behaviour is transcribed into
[`docs/ccd-switcher-behaviour.md`](docs/ccd-switcher-behaviour.md), the rail's
acceptance tests. Its surface recipe (layer, anchors, zone, collapse by resizing
the surface and its zone) survives in history in `app/cowl_surface_manager.cc`
(`git show 93d691a:shell/app/cowl_surface_manager.cc`).

No Views switcher has ever switched a workspace. The deleted lifted rail mirrored
live niri state but had no click path; the workspace strip in `tabs/` replaces it.

## Lifted files

| views-shell path | Source | sha256 (first 16) | Status |
|---|---|---|---|
| `shell/ozone/layer_shell/wayland_layer_shell.h` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e616eff3c2f93af6` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/wayland_layer_shell.h` |
| `shell/ozone/layer_shell/wayland_layer_shell.cc` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `840e97976602d27f` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/wayland_layer_shell.cc` |
| `shell/ozone/layer_shell/wayland_layer_shell_window.h` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell_window.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d8f94acd34245d3c` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/wayland_layer_shell_window.h` |
| `shell/ozone/layer_shell/wayland_layer_shell_window.cc` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell_window.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `2e953eff05bf2c4f` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/wayland_layer_shell_window.cc` |
| `shell/ozone/layer_shell/PROVENANCE.md` | agency-mvp `src/agency/ozone/layer_shell/PROVENANCE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e7c9f3cfb81ee34d` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/PROVENANCE.md` |
| `shell/ozone/layer_shell/BUILD.gn` | agency-mvp `src/agency/ozone/layer_shell/BUILD.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `bc3e0855aed2800b` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/layer_shell/BUILD.gn` |
| `shell/ozone/protocol/wlr-layer-shell-unstable-v1.xml` | agency-mvp `src/agency/ozone/protocol_xml/unstable/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e9b5304d17860358` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/ozone/protocol/wlr-layer-shell-unstable-v1.xml` |
| `shell/patches/agency-source.patch` | agency-mvp `patches/agency-source.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `1607d5bd05f209b3` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/agency-source.patch` |
| `shell/patches/agency-layershell-factory-fix.patch` | agency-mvp `patches/agency-layershell-factory-fix.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `ec76f15889246458` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/agency-layershell-factory-fix.patch` |
| `shell/patches/agency-layershell-popup.patch` | agency-mvp `patches/agency-layershell-popup.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b3b6dafbdf1dff11` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/agency-layershell-popup.patch` |
| `shell/patches/ozone-empty-opaque-region.patch` | agency-mvp `patches/ozone-empty-opaque-region.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `982a5344a43a5b0c` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/ozone-empty-opaque-region.patch` |
| `shell/patches/agency-build-gate.patch` | agency-mvp `patches/agency-build-gate.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `534731d9b5d2cc32` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/agency-build-gate.patch` |
| `shell/patches/agency-dbus-visibility.patch` | agency-mvp `patches/agency-dbus-visibility.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b47b36fb80357426` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/patches/agency-dbus-visibility.patch` |
| `shell/host/shell_host.h` | agency-mvp `src/agency/shell/shell_host.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `944dff5ffb77e2e0` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/host/shell_host.h` |
| `shell/host/shell_host.cc` | agency-mvp `src/agency/shell/shell_host.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `dd66b17a3e91d4fc` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/host/shell_host.cc` |
| `shell/rail/rail_controller.h` | agency-mvp `src/agency/shell/rail_controller.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `adde19c3b64f4ba8` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/rail/rail_controller.h` |
| `shell/rail/rail_controller.cc` | agency-mvp `src/agency/shell/rail_controller.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `bff31488434409d3` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/rail/rail_controller.cc` |
| `shell/rail/rail_view.h` | agency-mvp `src/agency/shell/rail_view.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `7574bfe259f613c0` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/rail/rail_view.h` |
| `shell/rail/rail_view.cc` | agency-mvp `src/agency/shell/rail_view.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `2f54d67885abfbbb` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/rail/rail_view.cc` |
| `shell/bar/bar_view.h` | agency-mvp `src/agency/shell/bar_view.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5e4109df9b25a634` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/bar/bar_view.h` |
| `shell/bar/bar_view.cc` | agency-mvp `src/agency/shell/bar_view.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `a12b442da5e64bed` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/bar/bar_view.cc` |
| `shell/bar/clock_controller.h` | agency-mvp `src/agency/shell/clock_controller.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `1b7d60784e580b27` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/bar/clock_controller.h` |
| `shell/bar/clock_controller.cc` | agency-mvp `src/agency/shell/clock_controller.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e720122f721980cd` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/bar/clock_controller.cc` |
| `shell/launcher/launcher_panel.h` | agency-mvp `src/agency/shell/launcher_panel.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `ce8763754f759b42` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/launcher/launcher_panel.h` |
| `shell/launcher/launcher_panel.cc` | agency-mvp `src/agency/shell/launcher_panel.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5715074d0bbd1845` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/launcher/launcher_panel.cc` |
| `docs/upgrade/VERIFIED-APIS.md` | agency-mvp `src/agency/shell/VERIFIED-APIS.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `31f8b0a23334aac5` | kept |
| `shell/wm/adapters/niri/niri_ipc_client.h` | agency-mvp `src/agency/signal/niri/niri_ipc_client.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `4f1488ddcac0be0e` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/niri_ipc_client.h` |
| `shell/wm/adapters/niri/niri_ipc_client.cc` | agency-mvp `src/agency/signal/niri/niri_ipc_client.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `28945c38233ccf75` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/niri_ipc_client.cc` |
| `shell/wm/adapters/niri/niri_events.h` | agency-mvp `src/agency/signal/niri/niri_events.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `944d3cde33a0c5b9` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/niri_events.h` |
| `shell/wm/adapters/niri/niri_events.cc` | agency-mvp `src/agency/signal/niri/niri_events.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `14497879d049c4fd` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/niri_events.cc` |
| `shell/wm/adapters/niri/BUILD.gn` | agency-mvp `src/agency/signal/niri/BUILD.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d9f5eeed85c476f9` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/BUILD.gn` |
| `shell/wm/adapters/niri/proof/niri_proto_probe.py` | agency-mvp `src/agency/signal/niri/proof/niri_proto_probe.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e9f54684567d020c` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/wm/adapters/niri/proof/niri_proto_probe.py` |
| `shell/tools/headless-eval.sh` | agency-mvp `tools/headless-eval.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5e1853a2be9fdd66` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/tools/headless-eval.sh` |
| `shell/tools/no_stubs.py` | agency-mvp `tools/no_stubs.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b19b34156fbad9c8` | kept (not yet wired into `tools/validate.sh`) |
| `shell/tools/no_stubs_fixtures/bad.cc` | agency-mvp `tools/no_stubs_fixtures/bad.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `13e4451968aa0f4b` | kept |
| `shell/tools/no_stubs_fixtures/bad.py` | agency-mvp `tools/no_stubs_fixtures/bad.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `0af1125fb3942ff7` | kept |
| `shell/tools/no_stubs_fixtures/good.cc` | agency-mvp `tools/no_stubs_fixtures/good.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `33529e224a0464bc` | kept |
| `shell/tools/no_stubs_fixtures/good.py` | agency-mvp `tools/no_stubs_fixtures/good.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `167fbf1d0e7e8d0e` | kept |
| `shell/build/args.gn` | agency-mvp `build/args.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e869a19271e30fe2` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/build/args.gn` |
| `shell/build/CHROMIUM_VERSION` | agency-mvp `build/CHROMIUM_VERSION` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d5e50af104143618` | kept; since edited to pin `154.0.8037.92` |
| `shell/build/wire-agency.sh` | agency-mvp `build/wire-agency.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `0af456fd3b879c80` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/build/wire-agency.sh` |
| `shell/build/wire-and-gen.sh` | agency-mvp `build/wire-and-gen.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `fdf261741ef55121` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/build/wire-and-gen.sh` |
| `shell/build/verify-compilation.sh` | agency-mvp `build/verify-compilation.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `6e1dbac0b6f64d1a` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/build/verify-compilation.sh` |
| `shell/build/LINK-GATE.md` | agency-mvp `build/LINK-GATE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `51eba848db99652e` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/build/LINK-GATE.md` |
| `docs/upgrade/CHROMIUM-UPGRADE.md` | agency-mvp `docs/CHROMIUM-UPGRADE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `8327c515be853017` | kept |
| `docs/upgrade/UPGRADE-LEDGER-150.md` | agency-mvp `docs/UPGRADE-LEDGER-150.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `c07791e750f7fe38` | kept |
| `shell/app/cowl_main.cc` | June 30 spike snapshot `src/cowl_main.cc` | `9ed26d32a848e9b4` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_main.cc` |
| `shell/app/cowl_main_delegate.h` | June 30 spike snapshot `src/cowl_main_delegate.h` | `585815e58687859a` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_main_delegate.h` |
| `shell/app/cowl_main_delegate.cc` | June 30 spike snapshot `src/cowl_main_delegate.cc` | `69c00e97652ba94f` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_main_delegate.cc` |
| `shell/app/cowl_content_browser_client.h` | June 30 spike snapshot `src/cowl_content_browser_client.h` | `9e84f86efe723c90` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_content_browser_client.h` |
| `shell/app/cowl_content_browser_client.cc` | June 30 spike snapshot `src/cowl_content_browser_client.cc` | `34849f2654f1ada5` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_content_browser_client.cc` |
| `shell/app/cowl_browser_main_parts.h` | June 30 spike snapshot `src/cowl_browser_main_parts.h` | `736ea2a00c4ddb65` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_browser_main_parts.h` |
| `shell/app/cowl_browser_main_parts.cc` | June 30 spike snapshot `src/cowl_browser_main_parts.cc` | `86f5c502691de4fa` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_browser_main_parts.cc` |
| `shell/app/cowl_surface_manager.h` | June 30 spike snapshot `src/cowl_surface_manager.h` | `32fc35236d3a2a75` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_surface_manager.h` |
| `shell/app/cowl_surface_manager.cc` | June 30 spike snapshot `src/cowl_surface_manager.cc` | `fc7674d95342dbfc` | deleted in chapter 2 (`0d30e89`); recover with `git show 93d691a:shell/app/cowl_surface_manager.cc` |

The hashes are of the lifted originals. `shell/tools/headless-eval.sh` gained a
`runtime-test` guard after the lift; its successor is
[`../tools/bench/worker/headless.sh`](../tools/bench/worker/headless.sh).

## What became of each lifted piece

| Piece | Outcome |
|---|---|
| `ozone/layer_shell/*`, `ozone/protocol/wlr-layer-shell-unstable-v1.xml` | Re-cut at `154.0.8037.92` into `patches/views-shell-ozone-layer-shell.patch` (T4): sources under `ui/ozone/platform/wayland/host/`, the XML under `third_party/wayland-protocols`, the configure-before-buffer state machine carried unchanged, the `ENABLE_COWL_LAYER_SHELL` buildflag and the ext-background-effect blur dropped. Deleted. |
| `patches/agency-source.patch`, `agency-layershell-factory-fix.patch`, `ozone-empty-opaque-region.patch` | Trimmed to layer-shell and folded into `views-shell-ozone-layer-shell.patch` (the empty opaque region scoped to translucent windows). Deleted. |
| `patches/agency-layershell-popup.patch` | Re-cut as `views-shell-ozone-layer-popup.patch` (T4, proven by `get_popup` on headless scroll). Deleted. |
| `patches/agency-build-gate.patch` | Re-cut as `views-shell-build-gate.patch`. Deleted. |
| `patches/agency-dbus-visibility.patch` | Not re-cut: one shared session-bus owner is a later task, which re-cuts it from history if it still needs the visibility edge. Deleted. |
| `patches/series.proposed` | Superseded by `patches/series`. Deleted. |
| `app/cowl_*` | Provenance only: a `//content` embedder (rule R24). Replaced by `app/views_shell_main.cc`, shaped like `ui/views/examples/examples_main_proc.cc`. Deleted. |
| `host/shell_host.*` | Its one lesson (teardown order: controller before widget) belongs to the shell main. Deleted. |
| `rail/*` | Replaced by the workspace strip in `tabs/`, built from Chrome's own tab (`../CHROME-PORT-LEDGER.md`). Deleted. |
| `bar/bar_view.*`, `bar/clock_controller.*` | Replaced by a new bar of stock Views pills fed by source plugins. Deleted. |
| `launcher/*` | Pattern only: the launcher is a stock `views::Textfield` and rows in a `ScrollView`. Deleted. |
| `wm/adapters/niri/*` | The template for the scroll adapter in `wm/adapters/scroll/`; the niri adapter of a later chapter reads it from history. Deleted. |
| `tools/headless-eval.sh` | Superseded by `tools/bench/worker/headless.sh`. Deleted. |
| `build/args.gn`, `wire-agency.sh`, `wire-and-gen.sh`, `verify-compilation.sh`, `LINK-GATE.md` | Superseded by `tools/bench/args.views.gn`, `tools/bench/worker/wire.sh` and `build.sh`, and the `assert_no_deps` guards in `BUILD.gn`. Deleted. |
| `build/CHROMIUM_VERSION` | Kept; pins the bench's tag. |
| `tools/no_stubs.py` and fixtures | Kept, to be wired into `tools/validate.sh`. |
| `docs/upgrade/*` | Kept as the lineage's upgrade record. |

## Never lifted

The producer band (audio, network, bluetooth, power, brightness, session, idle,
media, notifications, tray, polkit, the launcher index) and the shared D-Bus owner
from agency-mvp wait on decision P5. The whole-browser-on-a-layer-surface weld
patch, the `//chrome` shell-host patch (forbidden shape), the inbound Wayland
server, the web, V8 and Mojo machinery of the June embedder, the host-overlay
data protocols and every signal source for retired services were dropped at the
lift.
