# patches

The `views-shell-*.patch` files are the live series: `wire.sh` applies them,
in `series` order, with plain `git apply` against the pristine checkout at
the pinned tag (rule R20). The `agency-*.patch` and
`ozone-empty-opaque-region.patch` files are the untrimmed lifts from
agency-mvp `cd7cef3`, kept as provenance; they are never applied (Tom,
2026-10-01: "no need to 'apply cleanly'" — lifts are re-cut, never required
to apply from the lineage).

| Live patch | Re-cut from | Job |
|---|---|---|
| `views-shell-ax-automation-bindings.patch` | (new at T2, finding F1) | puts the AX Automation bindings behind `enable_ax_automation_bindings`, keeping `//v8` out of `//ui/views` |
| `views-shell-blink-renderer-edges.patch` | (new at T3, finding F3) | cuts the `blink_headers` and webnn-mojom-traits edges into `//v8` and the Blink renderer |
| `views-shell-build-gate.patch` | `agency-build-gate.patch` | root `gn_all` reaches `//views_shell` |
| `views-shell-ozone-layer-shell.patch` | `agency-source.patch` (layer-shell parts only) + `agency-layershell-factory-fix.patch` (folded) + `ozone-empty-opaque-region.patch` (folded, scoped to translucent windows) | `PlatformWindowType::kLayerShell`, the `zwlr_layer_shell_v1` binding and `WaylandLayerShellWindow` under `ui/ozone/platform/wayland/host/` (sources re-cut from `shell/ozone/layer_shell/`; the lift's `ENABLE_COWL_LAYER_SHELL` buildflag and the ext-background-effect blur are dropped, the configure-before-buffer state machine is carried unchanged), the init properties (plus the `Widget::InitParams::layer_shell` plumbing through `DesktopWindowTreeHostLinux`), the factory case, the vendored `wlr-layer-shell-unstable-v1.xml`, the empty opaque region |
| `views-shell-ozone-layer-popup.patch` | `agency-layershell-popup.patch` | `xdg_popup` children of layer surfaces: the xdg-parent walk stops at a layer surface, and `XdgPopup::Initialize` creates the popup with a NULL xdg parent and parents it via `zwlr_layer_surface_v1.get_popup` |

Not yet re-cut: `agency-dbus-visibility.patch` (one shared session-bus
owner; a later task).

Applied with plain `git apply`, from a pristine tree, in `series` order.
Never a three-way merge.
