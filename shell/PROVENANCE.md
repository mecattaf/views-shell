# Provenance of lifted code

Everything under `shell/` that is not a sketch was **lifted byte for byte** from
earlier prototypes by the same author, on 2026-10-01, following the lift list in
the scoping notes (the private scoping note `resurfaced-lift-list.json`, 61 entries). No file was
edited during the lift. Each keeps its original BSD-3 header and its original
identifiers, including retired lineage names. Renaming those is a separate,
later commit (PROPOSED, decision P4 in [`../docs/open-decisions.md`](../docs/open-decisions.md)),
so that this commit diffs cleanly against its sources.

## Sources

| Short name | Location | Revision | What it proved |
|---|---|---|---|
| agency-mvp | agency-mvp (private repository) | `cd7cef3`, branch `rail/v1-readonly-mirror`, which contains `origin/main` `d669028` and the layer-window activation fix `a1b4fc0` | `ninja agency chrome` green at Chromium **150.0.7871.124** with layer-shell on; a Views bar on niri; a launcher with typed input on headless sway; the rail as a read-only mirror of live niri state |
| June 30 spike | the private devlog archive (cade-spike0-workbench, `WB/`), subdirectory of the spike's architecture1 source snapshot | snapshot of 2026-06-30, Chromium **149.0.7827.102** | a content embedder without `//chrome` hosting Views on a layer surface with translucency, paint and keyboard (the four Views init gaps closed) |

The local `main` of agency-mvp is behind `origin/main`, so it was not used. No
Chromium tree or binary from either survives; every lift is source and patches.

## The niri workspace switcher that worked

The switcher that actually switched niri workspaces (2026-06-27) was **web content**
on a layer surface, driven by `niri msg` subprocesses. Its JavaScript is not lifted.
Its behaviour is transcribed into [`docs/ccd-switcher-behaviour.md`](docs/ccd-switcher-behaviour.md),
and becomes the rail's acceptance tests. Its surface recipe (layer, anchors, zone,
collapse by resizing the surface and its zone) survives in
`app/cowl_surface_manager.cc`, which the June 30 spike extended with the Views
bootstrap.

No Views switcher has ever switched a workspace. The lifted rail
(`rail/rail_controller.*`, `rail/rail_view.*`) mirrors live niri state but has no
click path. The first acceptance target is a rail click that switches a workspace
and is observed through the compositor's echo.

## Lifted files

| views-shell path | Source | sha256 (first 16) |
|---|---|---|
| `shell/ozone/layer_shell/wayland_layer_shell.h` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e616eff3c2f93af6` |
| `shell/ozone/layer_shell/wayland_layer_shell.cc` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `840e97976602d27f` |
| `shell/ozone/layer_shell/wayland_layer_shell_window.h` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell_window.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d8f94acd34245d3c` |
| `shell/ozone/layer_shell/wayland_layer_shell_window.cc` | agency-mvp `src/agency/ozone/layer_shell/wayland_layer_shell_window.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `2e953eff05bf2c4f` |
| `shell/ozone/layer_shell/PROVENANCE.md` | agency-mvp `src/agency/ozone/layer_shell/PROVENANCE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e7c9f3cfb81ee34d` |
| `shell/ozone/layer_shell/BUILD.gn` | agency-mvp `src/agency/ozone/layer_shell/BUILD.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `bc3e0855aed2800b` |
| `shell/ozone/protocol/wlr-layer-shell-unstable-v1.xml` | agency-mvp `src/agency/ozone/protocol_xml/unstable/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e9b5304d17860358` |
| `shell/patches/agency-source.patch` | agency-mvp `patches/agency-source.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `1607d5bd05f209b3` |
| `shell/patches/agency-layershell-factory-fix.patch` | agency-mvp `patches/agency-layershell-factory-fix.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `ec76f15889246458` |
| `shell/patches/agency-layershell-popup.patch` | agency-mvp `patches/agency-layershell-popup.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b3b6dafbdf1dff11` |
| `shell/patches/ozone-empty-opaque-region.patch` | agency-mvp `patches/ozone-empty-opaque-region.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `982a5344a43a5b0c` |
| `shell/patches/agency-build-gate.patch` | agency-mvp `patches/agency-build-gate.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `534731d9b5d2cc32` |
| `shell/patches/agency-dbus-visibility.patch` | agency-mvp `patches/agency-dbus-visibility.patch` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b47b36fb80357426` |
| `shell/host/shell_host.h` | agency-mvp `src/agency/shell/shell_host.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `944dff5ffb77e2e0` |
| `shell/host/shell_host.cc` | agency-mvp `src/agency/shell/shell_host.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `dd66b17a3e91d4fc` |
| `shell/rail/rail_controller.h` | agency-mvp `src/agency/shell/rail_controller.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `adde19c3b64f4ba8` |
| `shell/rail/rail_controller.cc` | agency-mvp `src/agency/shell/rail_controller.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `bff31488434409d3` |
| `shell/rail/rail_view.h` | agency-mvp `src/agency/shell/rail_view.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `7574bfe259f613c0` |
| `shell/rail/rail_view.cc` | agency-mvp `src/agency/shell/rail_view.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `2f54d67885abfbbb` |
| `shell/bar/bar_view.h` | agency-mvp `src/agency/shell/bar_view.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5e4109df9b25a634` |
| `shell/bar/bar_view.cc` | agency-mvp `src/agency/shell/bar_view.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `a12b442da5e64bed` |
| `shell/bar/clock_controller.h` | agency-mvp `src/agency/shell/clock_controller.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `1b7d60784e580b27` |
| `shell/bar/clock_controller.cc` | agency-mvp `src/agency/shell/clock_controller.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e720122f721980cd` |
| `shell/launcher/launcher_panel.h` | agency-mvp `src/agency/shell/launcher_panel.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `ce8763754f759b42` |
| `shell/launcher/launcher_panel.cc` | agency-mvp `src/agency/shell/launcher_panel.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5715074d0bbd1845` |
| `docs/upgrade/VERIFIED-APIS.md` | agency-mvp `src/agency/shell/VERIFIED-APIS.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `31f8b0a23334aac5` |
| `shell/wm/adapters/niri/niri_ipc_client.h` | agency-mvp `src/agency/signal/niri/niri_ipc_client.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `4f1488ddcac0be0e` |
| `shell/wm/adapters/niri/niri_ipc_client.cc` | agency-mvp `src/agency/signal/niri/niri_ipc_client.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `28945c38233ccf75` |
| `shell/wm/adapters/niri/niri_events.h` | agency-mvp `src/agency/signal/niri/niri_events.h` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `944d3cde33a0c5b9` |
| `shell/wm/adapters/niri/niri_events.cc` | agency-mvp `src/agency/signal/niri/niri_events.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `14497879d049c4fd` |
| `shell/wm/adapters/niri/BUILD.gn` | agency-mvp `src/agency/signal/niri/BUILD.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d9f5eeed85c476f9` |
| `shell/wm/adapters/niri/proof/niri_proto_probe.py` | agency-mvp `src/agency/signal/niri/proof/niri_proto_probe.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e9f54684567d020c` |
| `shell/tools/headless-eval.sh` | agency-mvp `tools/headless-eval.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `5e1853a2be9fdd66` |
| `shell/tools/no_stubs.py` | agency-mvp `tools/no_stubs.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `b19b34156fbad9c8` |
| `shell/tools/no_stubs_fixtures/bad.cc` | agency-mvp `tools/no_stubs_fixtures/bad.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `13e4451968aa0f4b` |
| `shell/tools/no_stubs_fixtures/bad.py` | agency-mvp `tools/no_stubs_fixtures/bad.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `0af1125fb3942ff7` |
| `shell/tools/no_stubs_fixtures/good.cc` | agency-mvp `tools/no_stubs_fixtures/good.cc` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `33529e224a0464bc` |
| `shell/tools/no_stubs_fixtures/good.py` | agency-mvp `tools/no_stubs_fixtures/good.py` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `167fbf1d0e7e8d0e` |
| `shell/build/args.gn` | agency-mvp `build/args.gn` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `e869a19271e30fe2` |
| `shell/build/CHROMIUM_VERSION` | agency-mvp `build/CHROMIUM_VERSION` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `d5e50af104143618` |
| `shell/build/wire-agency.sh` | agency-mvp `build/wire-agency.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `0af456fd3b879c80` |
| `shell/build/wire-and-gen.sh` | agency-mvp `build/wire-and-gen.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `fdf261741ef55121` |
| `shell/build/verify-compilation.sh` | agency-mvp `build/verify-compilation.sh` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `6e1dbac0b6f64d1a` |
| `shell/build/LINK-GATE.md` | agency-mvp `build/LINK-GATE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `51eba848db99652e` |
| `docs/upgrade/CHROMIUM-UPGRADE.md` | agency-mvp `docs/CHROMIUM-UPGRADE.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `8327c515be853017` |
| `docs/upgrade/UPGRADE-LEDGER-150.md` | agency-mvp `docs/UPGRADE-LEDGER-150.md` @ `cd7cef3` (branch rail/v1-readonly-mirror) | `c07791e750f7fe38` |
| `shell/app/cowl_main.cc` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_main.cc` | `9ed26d32a848e9b4` |
| `shell/app/cowl_main_delegate.h` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_main_delegate.h` | `585815e58687859a` |
| `shell/app/cowl_main_delegate.cc` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_main_delegate.cc` | `69c00e97652ba94f` |
| `shell/app/cowl_content_browser_client.h` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_content_browser_client.h` | `9e84f86efe723c90` |
| `shell/app/cowl_content_browser_client.cc` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_content_browser_client.cc` | `34849f2654f1ada5` |
| `shell/app/cowl_browser_main_parts.h` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_browser_main_parts.h` | `736ea2a00c4ddb65` |
| `shell/app/cowl_browser_main_parts.cc` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_browser_main_parts.cc` | `86f5c502691de4fa` |
| `shell/app/cowl_surface_manager.h` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_surface_manager.h` | `32fc35236d3a2a75` |
| `shell/app/cowl_surface_manager.cc` | June 30 spike snapshot `WB/cowl-src-spike0-architecture1/src/cowl_surface_manager.cc` | `fc7674d95342dbfc` |

Edited after the lift (each in its own commit): `shell/tools/headless-eval.sh` gained a
`runtime-test` guard and a socket-variable reset at the top. Its hash above is the
lifted original.

## How each lifted piece is meant to change

| Piece | Planned change (not done) |
|---|---|
| `ozone/layer_shell/*` | Keep the invariants byte for byte. Drop the blur-region setter and its build flag (scroll has no background-effect protocol). Add unit tests on the test Wayland server. Re-cut at the tracked Chromium tag. |
| `patches/agency-source.patch` | **Trim to layer-shell only**: drop the nine data-protocol targets, the host-overlay sources, background-effect and idle-notify. Fold `agency-layershell-factory-fix.patch` into it. Rename to `views-shell-ozone-layer-shell.patch`. |
| `patches/agency-layershell-popup.patch` | Becomes `views-shell-ozone-layer-popup.patch`. Live acceptance is still pending (explicit-grab caveat open). |
| `patches/ozone-empty-opaque-region.patch` | Becomes `views-shell-ozone-empty-opaque-region.patch`. Optionally scope to layer windows. |
| `patches/agency-build-gate.patch`, `agency-dbus-visibility.patch` | Rename the flag to `enable_views_shell` and paths to `//views-shell`. |
| `ozone/protocol/wlr-layer-shell-unstable-v1.xml` | Vendored only if the tree's `third_party/wayland-protocols` still lacks it. |
| `app/*` | **Provenance only.** This is a `//content` embedder (it still creates `WebContents`), and views-shell links no `//content` (rule R24). It is replaced by `views_shell_main.cc` shaped like `ui/views/examples/examples_main_proc.cc` (Ozone single-process, `mojo::core::Init`, ICU, `ResourceBundle`, `aura::Env`, `wm::WMState`), a `ShellMainParts`, a production `ShellContextFactory` (an in-process viz host after `InProcessContextFactory` and `components/viz/demo`), and `views_shell_views_delegate` with the rule guard. The surface table becomes the `SurfaceSpec` registry consumed by a `DesktopWindowTreeHostLinux` subclass, replacing the weld patch. |
| `host/shell_host.*` | Constructed from `ShellMainParts`, not from `//chrome` extra parts. Keep its teardown order (controller before widget). |
| `rail/rail_controller.*` | Split: the state fold becomes the compositor-neutral `WmModel`; the controller gains focus, move and rename through the adapter, redrawing on the echo. |
| `rail/rail_view.*` | Replaced by a WorkspaceNode tree after Chrome's vertical tabs. Its test snapshot becomes a unit-test fixture. |
| `bar/*` | Stock Views pills (a `views::Button` with `ImageView` and `Label` in a `FlexLayoutView`), themed by the mixer. Each pill is fed by a source plugin, like the quick-settings tiles. Ash's tray kit is pattern only. |
| `launcher/*` | Pattern only. The launcher is a stock `views::Textfield` and rows in a `ScrollView`, ranked by the ported `string_matching` (ledger A041). Ash's `app_list` views are pattern only. |
| `wm/adapters/niri/*` | Implements the `CompositorAdapter` interface and capability table, and gains the request socket (replacing `niri msg` subprocesses). Template for the scroll adapter. |
| `tools/headless-eval.sh` | **Done:** refuses to run outside `runtime-test` (PID 1 must be `bwrap`) and unsets every compositor socket variable. Still open: launch scroll instead of sway, once scroll is packaged. |
| `build/*` | Drop the inbound-server flag; set `dcheck_always_on = false` explicitly; add `assert_no_deps` on `//chrome`, `//ash`, `//chromeos`, `//content`, `//third_party/blink/renderer` and `//v8`; add a release, non-component configuration for footprint measurement. |

## Not lifted yet

The producer band (audio, network, bluetooth, power, brightness, session, idle,
media, notifications, tray, polkit, the launcher index) and the shared D-Bus owner
from agency-mvp, and the views-shell drafts, wait on decision P5. The lift list
names each of them.

## Dropped, per the lift list

The whole-browser-on-a-layer-surface weld patch and the `//chrome` shell-host
patch (forbidden shape), the inbound Wayland server, the web, V8 and Mojo
machinery of the June embedder, the host-overlay data protocols, and every
signal source for retired services.
