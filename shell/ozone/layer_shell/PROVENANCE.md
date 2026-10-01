# PROVENANCE — ozone/layer_shell

**Status: LIFTED VERBATIM (logic-faithful), permanent rebase carry.**

This directory is a byte-faithful logic lift of the proven cowl layer-shell
Ozone substrate (jun26 proven, jun28 M0-spike-validated on real DRM, jun30
Stage-2 layer-shell-Views GO). It is **not** a reimplementation: the cros/ash
"lift the interface shape, re-author the body" rule for the rest of the
`agency::` tree does **not** apply here, because this code never touched a
ChromeOS-only singleton in the first place — it is tier-2 `is_linux` Ozone
Wayland host code that already builds and runs correctly on a generic-Linux
DRM/Wayland stack (proven under seated greetd + niri, jun28/jun30).

## Source

| File (this dir)                  | Lifted from (cowl)                                                          |
|-----------------------------------|-------------------------------------------------------------------------------|
| `wayland_layer_shell.h`           | `cowl (private) patches/ozone/wayland_layer_shell.h`                |
| `wayland_layer_shell.cc`          | `cowl (private) patches/ozone/wayland_layer_shell.cc`               |
| `wayland_layer_shell_window.h`    | `cowl (private) patches/ozone/wayland_layer_shell_window.h`         |
| `wayland_layer_shell_window.cc`   | `cowl (private) patches/ozone/wayland_layer_shell_window.cc`        |

Chromium target paths (where these land in a patched tree):
- `ui/ozone/platform/wayland/host/wayland_layer_shell.{h,cc}`
- `ui/ozone/platform/wayland/host/wayland_layer_shell_window.{h,cc}`

Only include-path comment framing and a handful of prose comments
("COWL renders web content" → "agency renders shell content", `CowlSurfaceManager`
→ "the agency surface manager") were adapted. **No control flow, no state
machine, no method body was altered.** Diff the two trees with `diff -u` to
confirm: the only deltas are comment text and the top-of-file path banners.

## What is lifted and why each piece exists

1. **`WaylandLayerShell`** (the `zwlr_layer_shell_v1` global) — binds the
   compositor global via the `wl::GlobalObjectRegistrar<T>` pattern (same
   shape as `overlay_prioritizer.h` upstream), version-clamped to
   `kMaxVersion = 5`. Optional global: absence means `connection->layer_shell()`
   is `nullptr` and `kLayerShell` windows cannot be created — every call site
   must null-check.

2. **`WaylandLayerShellWindow`** (the `zwlr_layer_surface_v1` role object) —
   a `WaylandWindow` subclass, sibling to `WaylandToplevelWindow`. This is
   the actual per-surface state machine:
   - `OnInitialize()` captures the layer-shell properties
     (`layer_shell_layer`, `layer_shell_anchor`, `layer_shell_exclusive_zone`,
     `layer_shell_keyboard_interactivity`, margins, namespace, output id) off
     `PlatformWindowInitProperties` — these fields are added to that struct
     by the shared `cowl-source.patch` / agency equivalent, **outside this
     subdir's scope** (the orchestrator wires that shared header).
   - `CreateLayerSurface()` creates the `zwlr_layer_surface_v1` role from the
     existing `wl_surface`, sets size/anchor/exclusive-zone/margin/keyboard
     interactivity, then performs the **initial empty commit** (no buffer
     attached) and waits for the compositor's `configure` event.
   - `ConstrainedSize()` implements the protocol-mandated stretch rule:
     anchoring to opposite edges on an axis forces that dimension to `0`
     (compositor decides); otherwise the dimension must be non-zero.
   - Dynamic setters (`SetLayer`, `SetAnchor`, `SetExclusiveZone`,
     `SetMargin`, `SetKeyboardInteractivity`) send protocol requests
     immediately when a surface exists; Wayland double-buffers them to the
     next `wl_surface.commit`. `SetLayer` falls back to a destroy+recreate
     remap on pre-v2 compositors (no `set_layer` request available).
   - `SetBlurRegion()` is the `ext-background-effect-v1` integration,
     gated independently by the `COWL_HAS_BACKGROUND_EFFECT` BUILD.gn
     define (kept as-is; this is the agency equivalent of the cowl blur
     toggle and is orthogonal to the core layer-shell buildflag).

## Agency additions on top of the lift (net-new, NOT from cowl)

The two members below were added by **enabler #4 (issue #4, xdg_popup on
layer-shell surfaces)**. They are **agency-authored, net-new** — cowl never had
xdg_popup-on-layer, so there is no upstream line to lift here:

- `zwlr_layer_surface_v1* layer_surface() const` — read-only accessor over the
  existing private `layer_surface_` handle (`wayland_layer_shell_window.h`),
  exposed so `xdg_popup.cc` can parent an xdg_popup onto this layer surface via
  `zwlr_layer_surface_v1.get_popup`.
- `WaylandLayerShellWindow* AsWaylandLayerShellWindow() override` — the idiomatic
  `As*` down-cast (base declared in `wayland_window.h`), returning `this` so
  `GetXdgParentWindow()` stops the parent walk at a layer surface and
  `XdgPopup::Initialize()` takes the NULL-parent + `get_popup` branch.

These are the **only** two additions to the substrate. Both are additive and
read-only: the accessor returns the raw pointer without mutating it, and the
down-cast returns `this`. **Neither touches the configure-before-buffer
invariant** — the configure state machine (`IsSurfaceConfigured` /
`HandleSurfaceConfigure` / `ProcessPendingConfigureState` / `AckConfigure` and
`is_configured_`) is byte-unchanged. Parenting via `get_popup` creates no new
tracked object (it only associates an existing xdg_popup with this layer
surface), so teardown (`Hide()` → `DestroyLayerSurface()`) is likewise
unchanged.

## THE invariant (read this before touching anything in this directory)

**Configure-before-buffer.** `IsSurfaceConfigured()` returns `is_configured_`,
which starts `false` and only flips to `true` inside `AckConfigure()`, which
itself only runs after the compositor has sent the first `zwlr_layer_surface_v1.configure`
event (`OnConfigure()` → `HandleSurfaceConfigure()` →
`ProcessPendingConfigureState()` → eventually `AckConfigure()`). The GPU
process polls `IsSurfaceConfigured()` before it is allowed to create the
EGL surface and submit the first frame. If this gate is removed, weakened,
or short-circuited to `true` early (e.g. "just return true, it'll probably
be fine"), the GPU process creates the EGL surface and attaches a buffer
*before* the compositor has agreed on a size and committed to the surface
existing — Wayland protocol violation on strict compositors, and on niri
specifically this reproduces the **black screen** regression that the jun26
cowl substrate was built to fix. Every future rebase of this directory must
re-verify this exact call chain is intact byte-for-byte.

A second, smaller but related invariant lives just outside this directory's
scope (in the shared `wayland_surface.cc` opaque-region patch, see
`agency/patches/ozone-empty-opaque-region.patch`): stock Chromium advertises
a full-surface opaque region hint even on translucent layer-shell surfaces,
which makes niri skip compositing anything behind the surface — a second,
distinct cause of the same "black box" symptom. That patch is a 6-LOC hunk
outside this lift's scope but is part of the same proven-substrate story.

## Buildflag

`BUILDFLAG(ENABLE_COWL_LAYER_SHELL)` is kept **unrenamed** per ratified
decision D1.3: this is the same flag name the warm base (`cowl-source.patch`)
already wires through `//cowl/build:cowl_buildflags`, `wayland_object.{h,cc}`
trait declarations, and the `kLayerShell` dispatch arm in
`wayland_window_factory.cc`. Those wiring points are shared/aggregate files
outside this subdir's authoring scope (owned by the orchestrator), so this
lift intentionally does not introduce a parallel `ENABLE_AGENCY_LAYER_SHELL`
flag that would have to be kept in sync by hand.

## Not lifted here (net-new, different items)

- `ext_session_lock_manager_v1` / `WaylandSessionLockWindow` — net-new seam,
  not part of this lift.
- `xdg_positioner` popup routing — layer-shell parents cannot be an
  `xdg_toplevel` popup parent, so a menu/tooltip spawned from a bar must use
  `zwlr_layer_surface_v1.get_popup`. **DONE** in enabler #4: the routing lives
  **inline** in the existing `XdgPopup::Initialize` (no `WaylandPopupController`
  class was introduced), plus the `layer_surface()` / `AsWaylandLayerShellWindow`
  additions documented above. See `patches/agency-layershell-popup.patch`.
- `xdg_activation_v1` — absent from upstream Exo; net-new, required for
  launcher/dock focus handoff.
- `WlOutputTracker` / per-output SurfaceVariants hook into
  `WaylandOutputManager::OnOutputAdded/Removed` — net-new.

These are listed in the architecture spec
(`notes/views-shell-steelman/06-cpp-three-tier-architecture.md`, §2) as separate
Ozone seams to reimplement; they are out of scope for this lift item.
