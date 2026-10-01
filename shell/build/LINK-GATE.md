# The Agency Link-Gate

This document records exactly what was wired to pull the `//agency` additive
tree into the Chromium GN build graph so the producer band + Ozone lift + mojom
contracts + signal edge + identity core **compile as part of the tree**
(brave-style: an additive `src/agency/` tree reached by one group from the root
`gn_all`).

The whole gate builds with:

```
./build/wire-agency.sh --chromium <chromium>/src
# => gn gen out/agency && autoninja -C out/agency agency
```

---

## 1. `src/agency/BUILD.gn` — the top-level `agency` group

New file. Defines four groups:

- `group("agency")` — the link-gate root. Depends on `:producers`, `:signal`,
  `:identity`, `//agency/dbus:dbus`, `//agency/mojom:mojom`, and (arg-gated)
  `//agency/ozone/layer_shell`.
- `group("producers")` — every producer `source_set`. The **default,
  compile-clean band** (12 producers) plus two arg-gated deferred sets.
- `group("signal")` — `claude` + `niri` + `os`.
- `group("identity")` — `crypto` + `webbotauth`.

Every leaf is a real target that already exists in the tree (verified: each
`//agency/...` label resolves to a `source_set`/`mojom` target). No placeholder
targets.

### Default (green) producer band — 12 targets

`bluetooth, clock, idle, launcher, network, notifications, polkit, power,
session, tailscale, tray, update`

These build because their per-domain mojom is in the `//agency/mojom` aggregate
(or they own no mojom), and all their deps link on `is_linux`.

### Deferred bands — gated OFF by default, one arg each

| Arg (in `agency.gni`) | Default | Gates | Why it can't build yet |
|---|---|---|---|
| `agency_enable_pending_mojom_producers` | `false` | `audio`, `brightness`, `media` | Their sources `#include "agency/mojom/{Audio,Brightness,Media}.mojom.h"`, but those `.mojom` contracts **do not exist yet** — they are not in the `//agency/mojom` aggregate. Flip on once the contracts are authored. |
| `agency_enable_ash_capture` | `false` | `capture` | `CaptureModeDelegate` depends on `//ash/public/cpp` + `//chromeos/ash/services/recording/public/mojom`, both `assert(is_chromeos)`. Needs the one-line assert-loosening patches before it links on this is_linux fork. |
| `agency_enable_layer_shell` | `true` | `//agency/ozone/layer_shell` | Builds standalone **only** in a tree where `cowl-source.patch` has added the `wlr_layer_shell_protocol` / `ext_background_effect_protocol` targets and welded the `WaylandConnection` accessors. `wire-agency.sh` flips this to `false` automatically if that patch is not applied, so the core band stays green. |

This gating is real, complete GN (`if (arg) { deps += [...] }`), standard
Chromium practice — not a placeholder. It keeps `autoninja agency` green today
and turns each deferred item on with a single arg when its prerequisite lands.

---

## 2. `src/agency/mojom/BUILD.gn` — the one aggregate `mojom()` target

Edited. `sources` went from `[ "views-shell.mojom" ]` to the **eight** `.mojom` files
that exist in the directory:

```
views-shell.mojom, Bluetooth.mojom, Clock.mojom, Network.mojom,
Notifications.mojom, Power.mojom, Session.mojom, Tray.mojom
```

- All eight are `module agency.mojom`; the seven per-domain files
  `import "agency/mojom/views-shell.mojom";`. Because views-shell is in the **same** `mojom()`
  target, the intra-target imports resolve with no `public_deps` hop.
- `deps = []` / `public_deps = []` — verified none of the eight reference a
  `mojo_base` or `gfx/geometry` mojom type (Tray ships icons as a base64
  `data:image/png` string; Notifications carries `image_url` as a string).
- The generated header for each source is `agency/mojom/<Name>.mojom.h`
  (**case preserved** from the filename). Producers include exactly those
  paths and depend via `public_deps = [ "//agency/mojom:mojom" ]`.

**Excluded (by design):** `os.d.ts` is TypeScript, not a mojom.
`signal/claude/os_claude.mojom` lives under a different directory and is left to
its own future target (a `mojom()` source must resolve under the target's import
root; cross-directory aggregation is not done here).

---

## 3. The chrome-build hook

Three pieces, brave-style additive (modelled on the committed
`patches/cowl-source.patch` gn_all hunk):

1. **`patches/agency-build-gate.patch`** (new) — adds `"//agency:agency"` to the
   root `BUILD.gn` `group("gn_all")` deps, immediately above the existing
   `//cowl/src:cowl` line. This is the single edge that reaches the additive
   tree from the real build graph.
2. **`build/args.gn`** (edited) — added `enable_agency = true`. Drives
   `BUILDFLAG(ENABLE_AGENCY)` (see `agency_buildflags` below) and documents the
   toggle for the `out/agency` dir.
3. **DEPS / checkout** — the `//agency` tree is an additive source tree, so
   (like brave's `src/brave`) it is materialised into `<chromium>/src/agency/`
   rather than pulled by a gclient `DEPS` entry. `wire-agency.sh` does that
   `rsync`. If a gclient-managed layout is later preferred, the equivalent is a
   `DEPS` `deps["src/agency"]` entry pointing at this repo — the GN labels do
   not change either way.

### `src/agency/build/` — the recipe files the tree needs to gn-gen

- **`agency.gni`** (new) — the single `declare_args()` surface: `enable_agency`,
  `agency_enable_layer_shell`, `cowl_has_background_effect`,
  `enable_cowl_layer_shell`, `agency_enable_pending_mojom_producers`,
  `agency_enable_ash_capture`.
- **`BUILD.gn`** (new) — two `buildflag_header` targets:
  - `cowl_buildflags` — **required** by the committed
    `//agency/ozone/layer_shell` target, which `#include`s
    `"cowl/build/cowl_buildflags.h"` and gates on
    `BUILDFLAG(ENABLE_COWL_LAYER_SHELL)` / `BUILDFLAG(COWL_HAS_BACKGROUND_EFFECT)`.
    Emits at the unrenamed `cowl/build/` include path via `header_dir` (ratified
    decision D1.3 keeps the flag/header names unrenamed to match the warm base).
    Without this target the ozone dep was dangling — this closes it.
    **Coexistence:** in a tree where the full cowl embedder is *also* deployed,
    cowl owns `//cowl/build:cowl_buildflags`, which emits the same
    `cowl/build/cowl_buildflags.h`. Building both would double-generate that
    header, so `wire-agency.sh` detects a co-deployed `cowl/build/BUILD.gn` and
    sets `agency_enable_layer_shell=false` there (cowl already welds layer-shell
    into the real wayland target). Agency's `cowl_buildflags` is the provider
    only in the agency-standalone tree.
  - `agency_buildflags` — `BUILDFLAG(ENABLE_AGENCY)` for any tree code that wants
    to compile-time gate on the fork.

---

## 4. `build/wire-agency.sh`

New, executable, idempotent. Runs locally (`~/chromium/src`) and on ds4
(`--chromium <other-host>/chromium/src`). Steps:

1. `rsync src/agency/ -> <chromium>/src/agency/` (excludes `__pycache__`/`*.pyc`).
2. Add `//agency:agency` to root `gn_all` via `agency-build-gate.patch`
   (grep-guarded; python fallback insertion if the patch context drifted).
3. Layer-shell prerequisite: if `wlr_layer_shell_protocol` is absent and
   `--with-layer-shell` was not passed, set `agency_enable_layer_shell=false`
   so the core band still builds; with `--with-layer-shell` it applies
   `cowl-source.patch` first.
4. Write `out/agency/args.gn` (from `build/args.gn` + the resolved
   `agency_enable_layer_shell`), then `gn gen out/agency` and
   `autoninja -C out/agency agency`.

Flags: `--chromium PATH`, `--out DIR`, `--with-layer-shell`, `--gen-only`,
`--dry-run`.

---

## Content fixes required for the gate (applied here)

The gate is wiring, but two committed producer facts blocked compilation and are
fixed so the band is actually green:

1. **Missing `BUILD.gn` for `clock` and `bluetooth`.** These two producers
   shipped `.cc/.h` but no `BUILD.gn`, so the `agency` group could not depend on
   them. Authored `src/agency/producers/clock/BUILD.gn` and
   `.../bluetooth/BUILD.gn` matching the `session`/`network` style (deps
   `//agency/dbus:dbus` + `//base` + `//dbus`; public_deps `//agency/mojom:mojom`
   + `//mojo/public/cpp/bindings`, because each header includes the generated
   `Clock.mojom.h` / `Bluetooth.mojom.h` and uses `mojo::ReceiverSet`/`RemoteSet`).

2. **Mojom header case mismatch.** `Session.mojom` generates
   `agency/mojom/Session.mojom.h` (case preserved), but two files `#include`d the
   lowercase `agency/mojom/session.mojom.h`, which no rule generates:
   - `src/agency/producers/session/logind_session_producer.h`
   - `src/agency/producers/launcher/session_provider.cc`
   Both corrected to `Session.mojom.h`. (Stale lowercase references in the
   `session`/`power` `BUILD.gn` **comments** were also corrected for accuracy.)
   This unblocks the `session -> launcher -> update` chain.

No other producer source was modified.

---

## Verification checklist

- [x] Every `//agency/...` dep in `src/agency/BUILD.gn` resolves to a real
      `source_set`/`mojom` target (checked by grep over the sub-`BUILD.gn`s).
- [x] The `//agency/build:cowl_buildflags` dep of the committed ozone target now
      has a defining target.
- [x] No remaining lowercase-mismatched `#include "agency/mojom/*.mojom.h"`.
- [x] `bash -n build/wire-agency.sh` clean; `--dry-run` path exercised.
- [x] `gn gen out/agency && autoninja -C out/agency agency` on a real Chromium
      148 checkout: **GREEN** ("no work to do", exit 0) on ds4-worker
      2026-07-01. See `build/COMPILE-STATUS.md` for the green set, the two
      pre-existing arg-gated bands (capture, layer_shell), and the full
      Chromium-148 API-drift fix inventory.
