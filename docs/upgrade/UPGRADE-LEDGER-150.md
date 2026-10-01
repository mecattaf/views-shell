# Upgrade Ledger: Chromium 149.0.7827.232 → 150.0.7871.124

> **CONTINUING LEDGER — carried into agency-mvp, still open.**
> This is the live per-bump ledger for the 149→150 cycle, carried verbatim from
> `agency@f9203bf`. **Waves 0–3 are DONE** (new NixOS builder stood up; pristine
> 150 wire + `gn gen` green, 30437 targets, 0 FAILED / 0 --3way; patch set
> rebased). **§4b now records the wlr-layer-shell substrate recovery** — the
> gated stretch (#24), proven compile-green at 150 (gn gen + `autoninja
> :wayland` + `autoninja agency`, all exit 0). **§5 (cost summary) and §6 (proof
> of green) are now
> DONE** (Wave 5, 2026-07-16 evening, `worker-tb`): the full
> `ninja -k 0 -C out/agency agency chrome` linked `./chrome` with FAILED_COUNT=0,
> the incremental reprobe is `ninja: no work to do`, and the LIVE binary rendered
> the top bar on niri (evidence PNG + `niri msg --json layers` namespace proof).
> The "150-native, links, and runs" claim is now proven with evidence.
>
> *Scope note for agency-mvp:* this ledger records the **full** c150 cycle for
> honest cost accounting, including the `browser-features/*` rebases (§0b, §3).
> Those verticals are **dropped** from agency-mvp — the ledger keeps them only as
> the historical record of what the bump cost, not as a description of this
> repo's contents.

The honest rebase ledger for the **second** exercise of the LLM-assisted upgrade
protocol (issue #25, the c150 epic — an overnight opus-fleet run). Every patch
that needed reconciliation and every API-drift fix is recorded so the *cost of
staying bleeding-edge* is measured, not guessed. This cycle also carries a
**one-time builder cost**: the compile host moved from the old Fedora/`ds4` box
to a fresh NixOS worker (`worker-tb`), provisioned from blank as a native-FHS
Chromium builder — that setup is captured in `agency-agency/nix-chromium-builder`
and summarized in §0 below.

- **Base:** 149.0.7827.232 (`23cc2cb6`) → **Target:** 150.0.7871.124.
- **Method:** grep-guarded wire scripts on a PRISTINE 150 tree (`git reset --hard`
  before each authoritative wire, so every patch applies genuinely — no
  "already applied" skips masking drift). The real compiler on `worker-tb` is the
  gate.
- **Build config:** `agency_enable_layer_shell=false` (vanilla base; the
  layer-shell weld is the gated stretch, #24). `cc_wrapper="ccache"` (100 GiB).
- **Outgoing-149-tree snapshot (ledger baseline):**
  `worker-tb:~/agency-149-tree-mods-2026-07-16.patch` (6322 lines tracked mods) +
  `~/agency-149-untracked-2026-07-16.txt` (69 overlay entries).

---

## 0. One-time builder cost (new NixOS worker, Wave 0)

The 149 cycle built on Fedora/`ds4` inside a distrobox. This cycle stood up
`worker-tb` (NixOS 26.11, 32c/125 GiB) from **blank** as a native-FHS builder.
Repo: `agency-agency/nix-chromium-builder` (9 atomic commits, `dfa25dd..17280a9`).

| Step | Outcome |
|---|---|
| `buildFHSEnv` sandbox for Chromium's FHS-assuming toolchain | builds green; loader + depot_tools resolve |
| Shallow-safe tag fetch + DEPS-only `gclient sync` | 149 checkout green, 29 GiB |
| Full fork wire + `gn gen` at 149 | `WIRE-AND-GEN-OK`, 30437 targets, 0 FAILED / 0 --3way |
| Toolchain compile+link proof (`ninja base`) | `libbase.so` linked, rc=0 |

NixOS quirks surfaced (each a `nix-chromium-builder` commit): detach long jobs
via `systemd-run --user` (logind reaps ssh session scope); `ensure_bootstrap`
required (DEPOT_TOOLS_UPDATE=0 skips the gn/autoninja python bootstrap); pre-warm
gsutil/cipd before parallel `gclient sync`; no siso/remoteexec override needed at
149; ccache install + 100 GiB cap.

## 0b. Two features dropped as out-of-scope (mv2, adblock)

Both removed as peers — agency does not ship either. Net effect: the 150 rebase
surface shrank and one whole class of drift (vendored-rust-crate) disappeared.

- **mv2** (Wave 1, #19): the MV2-deprecation neutering feature. 150/151 complete
  the MV2 death, so keeping it alive is no longer a feature. `browser-features/mv2/`
  deleted, dropped from `INTEGRATION_ORDER`, README claim scrubbed. (The 149 cycle
  had to regenerate `mv2/01` via difflib — that cost is now zero.)
- **adblock** (owner scope call during Wave 3): agency does not provide native
  ad-blocking. Dropped the vendored Brave adblock rust engine (~768 files + 4
  patches). This removed the ONLY 150 rust-crate drift: (a) `arrayvec-v0_7` — a
  stock Chromium crate in 149 that 150 **deleted** from `//third_party/rust`, so
  the engine's `//third_party/rust/arrayvec/v0_7:lib` dep dangled; (b)
  `regex_automata/v0_4` which 150 newly marked `testonly = true`, blocking the
  non-test `regex/v1 -> regex_automata` edge the engine needed. Both vanished with
  the feature — no vendoring or testonly-override was needed. `adblock/02` and
  `adblock/03` were rebased in Wave 3 before the drop; that work was discarded.

## 1. Agency patch set (patches/)

| Patch | Outcome | Cause / fix |
|---|---|---|
| `agency-build-gate.patch` | **APPLIED CLEAN** | Root `gn_all` anchor stable 149→150. `//agency:agency` wired. |
| `agency-dbus-visibility.patch` | **APPLIED CLEAN** | `//components/dbus/thread_linux` visibility anchor unchanged. |
| `cowl-source.patch` | *(not wired — layer-shell off, vanilla base)* | Stretch #24 concern. |
| `ozone-empty-opaque-region.patch` | *(not wired — layer-shell off)* | Stretch #24. |
| `stage2-layershell-weld.patch` | *(not wired — reverted-to-pristine)* | Stretch #24 (the gated weld). |

## 2. Structural / upstream-refactor fixes (beyond re-anchoring)

- **adblock rust-crate drift → resolved by dropping the feature** (see §0b): 150
  deleted stock `arrayvec-v0_7` and marked `regex_automata/v0_4` testonly. No
  vendoring/override shipped — the feature is gone.
- **native-agent-api/05 histogram-enum re-collision** (upstream-refactor): 150
  again claimed enum slots in the fork's `BROWSER_OS_*` range (149 had the same);
  the fork block was renumbered above the new stock tail (verified via ssh grep).
- _(compile-phase API-drift → §4b, Wave 4.)_

## 3. Browser-feature patch rebases (browser-features/*)

Against a pristine 150 tree the wire surfaced **16 FAILED, 0 --3way** (drift too
hard for git's 3-way — pure re-anchor territory). Rebased via the empirical
`difflib` technique (protocol §4) against the accumulated wired tree, each
**independently adversarially verified** (a second agent re-ran `git apply --check`
+ faithfulness/no-stub checks). 14 rebased KEPT; 2 (`adblock/02`, `adblock/03`)
rebased then discarded with the feature.

| Feature / patch | Class | Cause → fix |
|---|---|---|
| **branding-settings/01-webui-url-constants** | context-drift | 150 inserted stock `kChromeUIGlicExperimentalOptIn*` between the anchor block and `kChromeUIHangUIHost`; re-anchored (h+cc). |
| **branding-settings/12-browser-buildgn-dep** | context-drift | `chrome/browser/BUILD.gn` deps list shifted by new stock `:universal_web_contents_observers` target; re-cut. |
| **url-privacy/01-chrome-browser-buildgn-deps** | context-drift | same `chrome/browser/BUILD.gn` list (cluster, wire order after branding/12). |
| **url-privacy/12-browser-prefs-register** | context-drift | `browser_prefs.cc` + `prefs/BUILD.gn` neighbours moved; re-anchored. |
| **cdp-domains/02-target-pdl** | upstream-refactor | `Target.pdl` domain restructured; adapted the tabId/windowId params. |
| **cdp-domains/04-devtools-buildgn** | context-drift | `devtools/BUILD.gn` sources/deps list drift. |
| **cdp-domains/08-session-h, 09-session-cc, 12-manager-delegate-h** | context-drift | `chrome_devtools_session.{h,cc}` + manager delegate anchors moved. |
| **cdp-domains/14-target-handler** | upstream-refactor | `content/browser/devtools/protocol/target_handler.cc` restructured. |
| **native-agent-api/05-histogram-values** | upstream-refactor | enum re-collision (see §2); renumbered fork block above new stock tail. |
| **degoogle-components/13-profiles-buildgn** | context-drift | `profiles/BUILD.gn` deps grew. |
| **agency-os-settings/01-webui-configs** | context-drift | `chrome_web_ui_configs.cc` registration list drift. |
| **agency-os-settings/05-lit-visibility** | context-drift | **hard gn-gen blocker** — `lit/v3_0:build_ts` visibility allowlist gained neighbours, shifting the `agency_settings` anchor → "Dependency not allowed" until re-anchored. |

**Method note:** the fan-out was a Workflow — 14 opus rebase agents + 14 sonnet
adversarial verifiers, 28 agents, 0 errors, 14/14 CONFIRMED. Authoritative pristine
re-wire: **WIRE-AND-GEN-OK** (0 FAILED, 0 --3way, gn gen green).

## 4b. Compile-phase fixes — wlr-layer-shell substrate at 150 (stretch #24)

A substrate agent brought the layer-shell Ozone weld green on a PRISTINE 150
tree (`worker-tb:~/chromium/src`, `9261fd0a`, 150.0.7871.124) with
`agency_enable_layer_shell=true` + `cowl_has_background_effect=true` (so the
blur path compiles, not moot). Six drift fixes were needed — all **gn / build
plumbing**, **zero C++ body edits**. They are the deltas landed in this repo
(FIX-1/3/6 in `patches/agency-source.patch`; FIX-2/4 in the `src/agency` band;
FIX-5 vendored under `src/agency/ozone/protocol_xml/` + wired by
`build/wire-agency.sh` step 3b).

| # | Category | File | Root cause → fix | Evidence (error cleared) |
|---|---|---|---|---|
| FIX-1 | gn dep missing | `ui/ozone/platform/wayland/BUILD.gn` (`:wayland` deps) | welded `host/*.cc` include `cowl/build/cowl_buildflags.h`, but the source patch added only the `wayland_protocol` deps, never the buildflag_header target → add `//agency/build:cowl_buildflags`. | header now resolves; M2 objects produced. |
| FIX-2 | gn label drift 148→150 | `src/agency/ozone/layer_shell/BUILD.gn` | at 150 wayland `common`/`host` are no longer own-BUILD.gn subdirs; they are source_sets `:common`/`:wayland` inside `ui/ozone/platform/wayland/BUILD.gn` → rewrite 3 labels (`…/common`→`…:common`, `…/host`→`…:wayland`). | `gn gen` GREEN (was: label-not-found). |
| FIX-3 | gn visibility wall | `ui/ozone/platform/wayland/BUILD.gn` (file-wide) | file-wide `visibility = [ "//ui/ozone/*" ]` blocks the standalone `//agency/ozone/layer_shell` target from depending on `:common`/`:wayland` → add `"//agency/*"` (same precedent as `agency-dbus-visibility.patch`). | "Dependency not allowed" cleared. |
| FIX-4 | gn import-path drift 148→150 | `src/agency/build/BUILD.gn` | `buildflag_header.gni` was flattened at 150 to `//build/buildflag_header.gni` (no `buildflag_header/` subdir) → update import. | "Unable to load …/buildflag_header/buildflag_header.gni" cleared. |
| FIX-5 | missing build input | `third_party/wayland-protocols/{staging,unstable}/` (10 XMLs) | the patch's 10 `wayland_protocol()` targets point at wlr/ext XMLs absent from the upstream wayland-protocols submodule of a fresh 150 checkout → vendor the 10 XMLs (repo `src/agency/ozone/protocol_xml/`, welded by wire step 3b). | ninja "missing and no known rule to make it" cleared; M2 GREEN. |
| FIX-6 | gn public_dep (propagation) | `ui/platform_window/BUILD.gn` | `platform_window_init_properties.h` is a PUBLIC header that includes `cowl/build/cowl_buildflags.h`; the buildflag dep must propagate to every consumer → `public_deps = [ "//agency/build:cowl_buildflags" ]`. | `platform_window_init_properties.h:18: fatal error: 'cowl/build/cowl_buildflags.h' file not found` cleared. |

**Milestones (evidence).** M1 `gn gen`: GREEN — `Done. Made 30953 targets from
4803 files`, exit 0, `agency_enable_layer_shell=true`. M2 `autoninja
ui/ozone/platform/wayland:wayland`: GREEN — exit 0, `no work to do` on
reconfirm; all 24 welded files produced objects. M3 `autoninja agency`: GREEN —
exit 0, 65/65 targets (producers + mojom + dbus + signal + identity +
layer_shell scaffold); standalone `//agency/ozone/layer_shell` compiled, no
duplicate-symbol / ODR / gn-cycle problems.

**Zero API drift in the 24 ported bodies.** The 4 substrate files
(`wayland_layer_shell{,_window}.{cc,h}`) and 20 protocol bodies
(`host_overlay/*.{cc,h}`) compiled **byte-identical** at 150 — no
148→150 descopes, no rework. All six fixes were gn/build plumbing; no
agency-band C++ source was modified.

**ODR / duplicate-symbol analysis (standalone vs welded).** The 4 substrate
`.cc` bodies are compiled twice — once as the welded copies in `:wayland`
(`host/wayland_layer_shell{,_window}.cc`) and once by the standalone lint target
`//agency/ozone/layer_shell` (same `COMPONENT_EXPORT` symbols). This is safe:
source_sets do not link, so M3 sees no duplicate symbols; and `gn path out/agency
//chrome:chrome //agency/ozone/layer_shell` returns **"No non-data paths found"**
— **chrome links the substrate ONLY via `:wayland`** (the ozone component). The
standalone target is reachable only from the `//agency:agency` link-gate group
(gn_all), which `//chrome` does not depend on, so a full-chrome link pulls the
objects exactly once. Resolution taken: leave the standalone target compiling (a
useful independent lint/scaffold; it caused no problem). If a future change makes
`//chrome` depend on `//agency:agency`, convert `//agency/ozone/layer_shell` to a
non-compiling holder (group / header-only) and let `:wayland` own the objects.

**`no_stubs.py` (M4).** The worker has no usable py3 libclang binding (no pip,
no network; the only `cindex.py` present is Python-2 syntax), so its AST backend
prints NONE — a pre-existing worker gap, not a code issue. The **coordinator**
ran `no_stubs.py` via `nix-shell -p python3Packages.libclang`. Without the
Chromium include roots libclang cannot resolve the fork's headers, so it emits
**parse false-positives**; the two it flagged (`SplitLines`, `QueryTallyStatus`)
were **verified false by inspection** (both are real, complete implementations —
not stubs). A lexical fallback scan of all 24 welded files for the exact markers
no_stubs flags (`NOTIMPLEMENTED|NOTREACHED|IMMEDIATE_CRASH|TODO|FIXME|XXX:`)
returned **zero hits**. To satisfy M4 as the gate intends, a py3 libclang binding
driven against the real Chromium include roots must be provisioned.

**Now link-verified (see §6).** As of Wave 5 (2026-07-16) the substrate is no
longer only compile-green: the full `ninja -k 0 agency chrome` linked `./chrome`
(48,159 steps, 0 failed), so the weld hunks + the `WaylandConnection` registrar
wiring survive the real link with no residual drift. The binary then rendered the
bar live on niri. M1–M3 held; the link and the live run now hold too.

## 5. Cost summary & iteration count — DONE (Wave 5, 2026-07-16)

Honest per-wave accounting of what the 149→150 cycle cost to reach a linking,
rendering binary. No estimate is inflated beyond the recorded evidence.

| Wave | Work | Cost (iterations / fixes) |
|---|---|---|
| **0** | New NixOS builder (`worker-tb`) stood up from blank (§0). | 9 atomic `nix-chromium-builder` commits; toolchain compile+link proof (`ninja base`, rc=0). |
| **0b** | Two verticals dropped as out-of-scope (mv2, adblock) (§0b). | removed the only rust-crate drift class; net-negative rebase surface. |
| **1–3** | Pristine 150 wire + `gn gen` green; patch set rebased (§1–§3). | 14 browser-feature patches rebased (`difflib`), each adversarially verified (28-agent fan-out, 0 errors, 14/14 CONFIRMED); authoritative re-wire **WIRE-AND-GEN-OK** (0 FAILED, 0 --3way). |
| **4b** | wlr-layer-shell substrate brought compile-green at 150 (§4b). | **6 gn/build-plumbing fixes, ZERO C++ body edits** — all pre-discovered by the substrate agent and already landed in this repo (`patches/agency-source.patch` FIX-1/3/6; `src/agency` band FIX-2/4; vendored XMLs FIX-5). The 24 ported bodies compiled byte-identical. |
| **5** | **Full chrome link + first live render (tonight).** | **ONE** full `ninja -k 0 -C out/agency agency chrome` — **48,159 steps, ZERO compile failures**, ended `LINK ./chrome`. **ONE** incremental 254-step rebuild after the mvp authoritative wire + weld/shell patches — again zero failures, `LINK ./chrome`. **ONE** runtime fix at the first live run: a `TYPE_WINDOW` DCHECK on the bar widget → `TYPE_WINDOW_FRAMELESS` (merged PR #16). |

**Iteration count to first win.** From a pristine 150 tree the substrate needed
**6 pre-discovered gn-level fixes** (none re-litigated tonight), **0** compile
iterations across the 48k-step build (it went green on the first full run), and
**1** runtime DCHECK fix at the first live launch. The layer-shell C++ bodies
required **no** 148→150 rework.

## 6. Proof of green (150.0.7871.124 on worker-tb) — DONE (Wave 5, 2026-07-16)

All lines below verified LIVE on `worker-tb` the evening of 2026-07-16 against a
wired pristine `150.0.7871.124` tree with `agency_enable_layer_shell=true`.

**Full build — links `./chrome`.**
- `ninja -k 0 -C out/agency agency chrome` → **48,159 steps, FAILED_COUNT=0**,
  ends with `LINK ./chrome`. (log: `worker-tb:/tmp/ninja.log`.)
- Incremental reprobe → **`ninja: no work to do`**, exit 0 (nothing under-built).
- Binary present: `out/agency/chrome` = **746,905,112 bytes, Jul 16 19:13**.

**Incremental rebuild after the mvp authoritative wire.**
- mvp repo `@ fab19a7` export → `wire-agency.sh --gen-only --with-layer-shell`
  from PRISTINE → all patches **strict-applied** (build-gate, dbus-visibility,
  agency-source, factory-fix, popup), **24 host sources welded, 10 XMLs
  vendored, gn gen green**. Then opaque-region + layer-shell-weld + shell-host
  applied (the wire gap now closed by §3 of PR "first win").
- Rebuild after those patches: **254 steps, 0 failed, `LINK ./chrome`**.
  (log: `worker-tb:/tmp/ninja2.log`.)

**First live run — the bar renders on niri.**
- Second launch (after the PR #16 `TYPE_WINDOW_FRAMELESS` fix): unit
  `agency-bar` ACTIVE; stderr, in order:
  - `[agency] welded 'BrowserWidget' onto wlr-layer-shell (ns=agency-browser)`
  - `[agency] --agency-layer-shell: building ShellHost`
  - `[agency] welded 'agency:bar' onto wlr-layer-shell (ns=agency-bar)`
- niri placement proof — `niri msg --json layers`:
  ```json
  {"namespace":"agency-bar","output":"DP-1","layer":"Top","keyboard_interactivity":"None"}
  ```
  plus `agency-browser` at `Top` / `OnDemand`.
- **Pixel proof:** `niri msg action screenshot-screen` (niri 26.04) captured the
  LIVE chromium output — the bar strip with a centered live clock reading
  **`7:28PM`** above the full-bleed browser NTP. Artifact:
  [`docs/evidence/2026-07-16-topbar-first-win.png`](evidence/2026-07-16-topbar-first-win.png).

**Screenshot-capture caveat (recorded, honest).** The jun30 wlr-screencopy/GBM
capture failure did **not** reproduce here: `worker-tb`'s output is a **virtual
display**, so everything composited and the capture succeeded. On a **real-GPU**
box (Tom's AMD box) direct scanout may still hide client buffers from an
output-screenshot; the `disable-direct-scanout` + `WaylandOverlayDelegation`
experiment matrix in issue #5 remains the documented lever for that environment.
It was **untested tonight because unnecessary here** — flagged, not claimed.
