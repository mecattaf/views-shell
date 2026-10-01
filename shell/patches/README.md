# patches

The `views-shell-*.patch` files are the live series: `tools/bench/worker/wire.sh`
applies them, in [`series`](series) order, with plain `git apply` against the
pristine checkout at the pinned tag (rule R20). Never a three-way merge. Nothing
else lives here: the untrimmed lifts they were re-cut from were deleted in
chapter 2 and are recorded in [`../PROVENANCE.md`](../PROVENANCE.md).

| Live patch | Origin | Job |
|---|---|---|
| `views-shell-ax-automation-bindings.patch` | new at T2, finding F1 | puts the AX Automation bindings behind `enable_ax_automation_bindings`, keeping `//v8` out of `//ui/views` |
| `views-shell-blink-renderer-edges.patch` | new at T3, finding F3 | cuts the `blink_headers` and webnn-mojom-traits edges into `//v8` and the Blink renderer |
| `views-shell-build-gate.patch` | re-cut of the lifted build gate | root `gn_all` reaches `//views_shell` |
| `views-shell-ozone-layer-shell.patch` | re-cut at T4 of the lifted layer-shell source patch, its factory fix and the empty-opaque-region patch | `PlatformWindowType::kLayerShell`, the `zwlr_layer_shell_v1` binding and `WaylandLayerShellWindow` under `ui/ozone/platform/wayland/host/` (the lift's `ENABLE_COWL_LAYER_SHELL` buildflag and the ext-background-effect blur dropped, the configure-before-buffer state machine carried unchanged), the init properties (plus the `Widget::InitParams::layer_shell` plumbing through `DesktopWindowTreeHostLinux`), the factory case, the vendored `wlr-layer-shell-unstable-v1.xml`, and an empty opaque region for translucent windows |
| `views-shell-ozone-layer-popup.patch` | re-cut at T4 of the lifted layer-shell popup patch | `xdg_popup` children of layer surfaces: the xdg-parent walk stops at a layer surface, and `XdgPopup::Initialize` creates the popup with a NULL xdg parent and parents it via `zwlr_layer_surface_v1.get_popup` |

A new patch is added to `series` and to this table in the same commit, and must
apply with plain `git apply` on the pristine tree after the ones before it.
