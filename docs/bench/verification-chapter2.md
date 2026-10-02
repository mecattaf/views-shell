# Chapter 2 re-verification (fable-verify, 2026-10-02)

An adversarial re-run of every chapter-2 claim on `origin/main` at `7131e49`
(PR #34 merged), after the three waves. Nothing here was fixed; a prove command
that is wrong while its claim is true is named as such with the corrected
command. The bench lock was held for the whole session
(`tools/bench/lock.sh hold`, 01:28Z to the end), the tree was wired from this
worktree (`~/views-shell-wt/w-verify-ch2`, main byte for byte), `out/views` and
`out/release` were rebuilt from it, and every wave sequence was re-run from the
same wiring as unit `vs-verify-ch2` (driver `~/views-bench/verify/verify-ch2.sh`,
logs `~/views-bench/logs/verify-ch2.log` and `verify-<item>.log`, results under
`~/views-bench/results/verify*` and the items' own result directories, all
rewritten). Then `tools/prove.sh --latest` re-ran every bench row from the
coordinator inside the hold (`P(9|1[0-9]|2[0-3])\.`: PROVE-RUN 27/28, the one
fail being the lock-taking P10.5, re-run green after the release), every local
row (PROVE-RUN 30/30) and the chapter-1 run rows (`^P(2|3|4)\.`: 10/11, P3.4
superseded).

The first pass of the driver ran as a unit with the `job.sh` PATH
(`/run/wrappers/bin:/etc/profiles/per-user/$USER/bin:...` with `$USER` left
unexpanded by `systemd-run`): `bwrap` was not on PATH, every `headless.sh`
printed `runtime-test: bubblewrap is required` and ran nothing. The wave units
had no `Environment=` at all (checked with `systemctl --user show vs-w3c-seq`),
so the second pass used that recipe and inherited the user manager's PATH. Every
number below is from the second pass. The wire, build and factory steps of the
first pass are valid (no compositor was involved) and are cited as such.

## Verdicts

Verdicts: **confirmed** (the claim holds on a fresh run), **confirmed, stored**
(the row's command only reads a log or rc file written by the wave's unit; the
claim was re-proven by the fresh sequence named in the evidence), **confirmed,
command wrong** (the claim holds but the row's command does not prove it),
**superseded**, **refuted**. Flakiness counts every fresh run of the same
command today: `n/n` means all drew or passed.

| Claim | Verdict | Re-run command | rc | Decisive evidence | Flakiness |
|---|---|---|---|---|---|
| C9.1 production factory, no test_support | confirmed | pass-1 `factory` step: `gn desc out/views //views_shell:views_shell testonly`, `deps --all \| grep -c test_support`, five `gn path` | 0 | `testonly=false deps_all=6023 test_support=0 base_test=0 compositor_test=0`, `gn path out/views no-browser: 5 of 5`, `ldd` (inside the FHS env, census) 324 libraries, 0 named test_support; P9.1 is a prose row, run by hand as the same three commands | 1/1 |
| C9.2 production bar draws | confirmed | P9.2 | 0 | `attaches: 2`, `colours: 3`, `1 zwlr_layer_shell_v1#11.get_layer_surface`, `ack_configure: 4`, `in-process viz up: software compositing`; `PSS_MB=109.2 threads=17 FDs=361` (w1a seq) | 3/3 (w1a seq, P9.2, P4.2) |
| C9.3 popup and no-flag runs | superseded (popup confirmed) | `seq/w1a.sh runs` | 1 | popup: `1 xdg_surface#46.get_popup`, `attaches: 14`, `colours: 51`; no flags: `views-shell draws only on layer surfaces (rule R1)`, exit 2, `attaches: 0` — see Superseded | popup 2/2; plain 0/2 by design |
| C9.4 unittests umbrella runs | confirmed | P9.4 | 0 | `SUCCESS: all tests passed.`; today 139 tests, 132 OK, 7 SKIPPED (the row's recorded "No matching tests" is history); the command runs in the FHS env with the bench user's live `DBUS_SESSION_BUS_ADDRESS`, so the 4 `NotificationServerBusTest` cases claimed `org.freedesktop.Notifications` on that bus | 2/2 |
| C10.1 release build exists | confirmed, stored | P10.1 (reads `w1b-seq.rc`); fresh: `ensure-out.sh release` + `autoninja -C out/release views_shell views_examples` | 0 | `ENSURE-OK release unchanged`, `[553/553] LINK ./views_examples`, both executables, `is_component_build = false` | 1/1 |
| C10.2 release: no browser labels | confirmed | P10.2 | 0 | prints `5` | 2/2 (P10.2, w1b prove) |
| C10.3 release bar draws | confirmed | P10.3 | 0 | `attaches: 2`, `colours: 3`, `PSS_MB=60.5 threads=17 FDs=68` | 3/3 (w1b seq, w1b prove, P10.3) |
| C10.4 release record | confirmed | P10.4 | 0 | 4 `PSS_MB=` rows; the record's 81.1 MB / 30 threads / 55 FDs is the chapter-1 test-factory binary, see Numbers | local |
| C10.5 ensure-out idempotent | confirmed, command wrong inside a hold | P10.5 | 0 | `ENSURE-OK views unchanged` (twice in the driver, once after the lock was released); inside `lock.sh hold` the row waits on `flock -w 3600` (rc 255 when killed after 193 s), as w2e documented | 3/3 |
| C11.1 FD census | confirmed | P11.1 | 0 | `fd kinds: socket=5 anon_inode:[eventfd]=10 pipe=4 memfd=3 /dev/shm=0 file=331 other=8`, `fd per process: 36:bwrap=4 47:views_shell=357`; recount of `fds-47.txt`: 323 `.so` (276 under `out/views`, 51 system) + the executable + `icudtl.dat` + 2 `ui_test.pak` + 2 `client.log`; recheck at 60 s identical | 2/2 |
| C11.2 runtime-test-gpu on the bench only | confirmed | P11.2 | 0 | `by-path card1 renderD128`; coordinator wrapper has no `allow-dri` | 1/1 |
| C11.3 gles2 compositor + default client GL draws | confirmed | P11.3 | 0 | `renderer: WLR_RENDERER=gles2 ... by-path card1 renderD128`, `attaches: 1`, `colours: 3`; `PSS_MB=129.5 threads=16 FDs=360` (w1c seq) | 2/2 |
| C11.4 GPU record and T4 PNGs | confirmed | P11.4 | 0 | `4`; both PNGs present; the four 6970-byte bar PNGs in docs/bench are one file (md5 `8430f80f…`) and so are the 8 wave-1 `shot.png` captures on the bench: the frame is deterministic, not copied | local |
| C12.1–C12.4 conformance | confirmed | P12.1–P12.4 | 0 | `CONFORMANCE-OK example.echo`, `…power-menu`, `…quick-settings-brightness`; the protocol-2 copy fails rc 1 | local |
| C13.1–C13.4 lift deleted | confirmed | P13.1–P13.4 | 0 | all nine paths absent; `shell/PROVENANCE.md:46`; `fences: clean`; empty patch diff | local |
| C14.1 adapter tests | confirmed | P14.1 | 0 | 18 `[ OK ]` (`OK lines: 18`), `SUCCESS` | 2/2 (seq, P14.1) |
| C14.2 wm_probe switch echo | confirmed | P14.2 | 0 | `SWITCH workspace 3 (by name)`, `ECHO workspace 3`, tree `"name": "3"`; also `--switch 5` → `ECHO workspace 5`; with `SCROLLSOCK` and `SWAYSOCK` unset the probe still connects (the client's `$XDG_RUNTIME_DIR` scan finds the private socket) | 4/4 |
| C14.3 wm_probe dump keys | confirmed, command wrong | P14.3 | 0 | the row prints `bash: /tools/bench/worker/headless.sh: No such file or directory` (its `$R` is set by P14.2, not by itself) and then passes on the dump.json of the previous run. The fresh seq probe (`STEP probe-dump rc 0`, `dump keys: [...]`) proves the claim. Corrected: `R=$(tools/bench/sync.sh) && ssh worker "rm -f ~/views-bench/results/w2a-dump/dump.json; bash $R/tools/bench/worker/headless.sh ~/views-bench/results/w2a-dump ~/views-bench/build-env -c '~/chromium/src/out/views/wm_probe --dump > ~/views-bench/results/w2a-dump/dump.json'; python3 -c '…same check…'"` | 1/1 fresh |
| C14.4 no home paths in shell/wm | confirmed | P14.4 | 0 | nothing; `VALIDATE-OK` | local |
| C15.1 theme and bar tests | confirmed, stored | P15.1 (reads `w2b-seq.log`); fresh `seq/w2b.sh tests … claims` | 0 | 27 OK incl. `ThemeFixtureTest.{AllBlack,ClaudeDark,ClaudeLight,Noir,EveryFixtureInTheDirectory}`, `STEP claim-C15.1 rc 0` | 2/2 |
| C15.2 claude-dark bar ground | confirmed, stored | P15.2; fresh `w2b.sh` prove+claims | 0 | `bar-bg-fraction claude-dark 1a1a1a 0.9851` (was 0.9976 before the strip's pill sat in the bar), alive 1, attaches 2 | 2/2 |
| C15.3 all-black, no theme | confirmed, stored | P15.3 | 0 | `bar-bg-fraction all-black 000000 0.9852`, plain `attaches: 3 colours: 3`, `STEP prove-no-toplevel rc 0` | 2/2 |
| C15.4 no surface flag exits 2 | confirmed | P15.4; fresh `runtime-test -- build-env -c 'views_shell … --run-for-seconds=2'` | 0 | `views-shell draws only on layer surfaces (rule R1); pass --bar (or --left-tabs)`, `rc=2` (claims stage, prove stage, and the driver's `r1-exit-2`) | 3/3 |
| C15.5 theme goldens | confirmed | P15.5 | 0 | `theme-goldens: ok (4 themes)`, `VALIDATE-OK` | local |
| C16.1 notification tests with display and bus | confirmed, stored | P16.1; fresh `seq/w2c.sh display` | 0 | `display: wayland-1 bus: unix:path=/tmp/dbus-…`, `OK lines: 18 SKIPPED lines: 0 FAILED lines: 0` | 1/1 (+ 139/139 in the whole-suite display run) |
| C16.2 bus tests skip without a bus | confirmed, stored | P16.2; fresh `seq/w2c.sh nobus` | 0 | 8 OK, 4 `[ SKIPPED ] NotificationServerBusTest.*` | 1/1 |
| C16.3 notifications deps | confirmed, stored | P16.3; fresh `seq/w2c.sh graph` | 0 | `message_center: 1 dbus: 1 components/dbus: 0`; `transitive deps: 5923, under //components/dbus: 6` | 1/1 |
| C16.4 popups are layer-shell | confirmed | P16.4 | 0 | `shell_message_popup_collection.cc:97: init_params->layer_shell = …`; `## Not done` | local |
| C17.1–C17.4 registry and render goldens | confirmed | P17.1–P17.4 | 0 | 12 registry views and 14 traces byte for byte; negatives exit 1 | local |
| C18.1 local runner | confirmed | `tools/prove.sh --latest --form local --ids 'P(9\|1[0-9]\|2[0-3])\.'` | 0 | `PROVE-RUN 30/30`; P18.1 itself nested | local |
| C18.2 lint | confirmed | P18.2 | 0 | `prove-lint ok (141 rows)` before this PR's rows | local |
| C18.3, C18.4 dry run and negative lint | confirmed | P18.3, P18.4 | 0 | `PROVE-DRY` without ssh; the altered row named | local |
| C18.5 lock status | confirmed | P18.5 | 0 | `not held` before the hold; during the hold `held by pid N: flock -w 14400 …` and `this checkout: holding` | 2/2 |
| C19.1 ui_tree tests | confirmed, stored | P19.1; fresh `seq/w3a.sh plain display` | 0 | plain `OK lines: 37 SKIPPED 2`, display `OK lines: 39 SKIPPED 0`, 14 `RenderTraceTest.MatchesGolden/*` | 1/1 |
| C19.2–C19.4 render goldens, no colour literal, not-drawn list | confirmed | P19.2–P19.4 | 0 | `render ok (14)`; nothing printed; 5 bullets | local |
| C20.1 plugin tests | confirmed | P20.1 | 0 | 30 `[ OK ]`, three `[ PASSED ] 10 tests.` | 3/3 (seq tests, claims, P20.1) |
| C20.2 echo plugin ping | confirmed | P20.2 | 0 | `invoke example.echo/ping {"text":"hi"} -> result {"result":{"args":{"text":"hi"}}}`, `launch path: scope`, `shutdown exit 0`; inside runtime-test `launch path: plain` | 4/4 |
| C20.3 undeclared exec refused | confirmed | P20.3 | 0 | `-32001 … "data":{"permission":"exec:true"}`, `plugin alive: true` | 3/3 |
| C20.4 validate | confirmed | P20.4 | 0 | `registry ok (12)`, three `CONFORMANCE-OK`, `VALIDATE-OK` | local |
| C21.1 workspace switch through WmModel | confirmed | P21.1 | 0 | `demo-workspace-switch: FocusWorkspace 3`, `wm: focused workspace 3`, `ECHO workspace 3`, tree `"name": "3"`, no `"app_id": "views-shell"`, `colours: 13`; starting on workspace 7 (`scrollmsg workspace 7` first) the same lines; with `SCROLLSOCK`/`SWAYSOCK` unset: `wm: no compositor socket (...); the bar runs without workspaces`, `demo-workspace-switch: no compositor adapter; nothing to switch`, the bar still draws (`attaches: 14`) | 4/4 (+1 degraded by design) |
| C21.2 notification popup is a layer surface | confirmed | P21.2 | 0 | `2 zwlr_layer_shell_v1#11.get_layer_surface`, `get_layer_surface(… 3, "views-shell-notification")` | 4/4 |
| C21.3 typed input on a layer surface | confirmed | P21.3 | 0 | `keyboard-probe: widget active 1`, `textfield focused 1`, `TYPED h` … `TYPED hello`, `wtype rc 0`; also on workspace 7 and with no `SCROLLSOCK`; the screenshot shows `hello` in the field, the popup and the `1` pill | 6/6 |
| C21.4 strip tests | confirmed | P21.4 | 0 | `[ PASSED ] 7 tests.` (and 2 + 10 in the build stage's `WorkspaceStrip*:Bar*:Clock*`) | 2/2 |
| C21.5 assembled record | confirmed | P21.5 | 0 | rows and 3 PNGs present; numbers reproduced within 1 MB, see Numbers | local |
| C22.1–C22.4 plugin docs and examples | confirmed | P22.1–P22.4 | 0 | `VALIDATE-OK`; 358 lines; every plugin dir has a README; empty diff | local |
| C2.1 bench tag and views_examples | confirmed | P2.1 | 0 | `154.0.8037.92` | 1/1 |
| C2.2 views_examples: no browser labels (F1) | confirmed | P2.2 | 0 | `4` | 1/1 |
| C2.3 build env | confirmed | P2.3 | 0 | path contains `views-shell-build-env` | 1/1 |
| C2.4 views_examples runs, attaches nothing (F2) | confirmed | P2.4 (the t2 worktree's older `headless.sh`) | 0 | `get_toplevel`, `client alive at capture: 1`, `attaches: 0`, `colours: 1`, `PSS_MB=161.5 threads=61 ctxsw/s=202.7` | 3/3 (P2.4, w1b, w1c controls) |
| C2.5, C3.5 bench records | confirmed | P2.5, P3.5 | 0 | `PSS_MB=` rows present | local |
| C3.1, C3.2 t3 build and wire | confirmed, stored | P3.1, P3.2 (read `t3-build.rc`, `t3-wire.log`) | 0 | `rc 0`, `WIRE-OK`; the units are chapter-1 history; today's wire of main is below | stored |
| C3.3 views_shell: no browser labels | confirmed | P3.3 | 0 | `5` | 2/2 |
| C3.4 no-flag window draws | superseded | P3.4 | 1 | `client alive at capture: 0`, `attaches: 0`, log `views-shell draws only on layer surfaces (rule R1)`; exit code 2 confirmed by the driver and by w2b's claims stage | 0/3 by design |
| C4.1 layer-shell patch builds | confirmed, stored | P4.1 (reads `t4-wire.log`, `t4-build.rc`) | 0 | `rc 0`; today: `WIRE-OK 334b65d254 28 changed paths`, both builds green | stored |
| C4.2 bar is a layer surface | confirmed | P4.2 (t4 worktree's `headless.sh`) | 0 | `1 zwlr_layer_shell_v1#11.get_layer_surface`, `4 zwlr_layer_surface_v1#39.configure`, `attaches: 2`, `colours: 3` | 3/3 |
| C4.3 menu becomes an xdg_popup | confirmed | P4.3 | 0 | `1 xdg_surface#46.get_popup`, `attaches: 14`, `colours: 51` | 3/3 |

## Checks beyond the rows

- **Wire from main on a pristine checkout:** `wire.sh` printed `applied` for
  all five patches and `WIRE-OK 334b65d254 28 changed paths`. `git status
  --porcelain` after the wire lists exactly the 27 paths the series touches
  (20 modified, the untracked `third_party/wayland-protocols/unstable/wlr-layer-shell/`
  directory for the vendored XML) plus `views_shell/`; nothing else.
- **views_shell_unittests whole:** `--gtest_list_tests` lists 139 tests in 33
  suites (`RenderTraceTest.MatchesGolden` is 14 instances). In the FHS env with
  no display, twice: 132 OK, 7 SKIPPED, 0 FAILED, 0 crashes, identical lists.
  The 7 skips: `NotificationServiceTest.StartWithoutBusThenStop`,
  `ShellMessagePopupCollectionTest.{PopupAppearsOnNotify,PopupsStackTopDown,CloseNotificationClosesThePopup,ActionButtonsMapToActionKeys}`
  (no Wayland display) and
  `UiTreeRendererTest.{InputsFireResolvedActions,TextfieldFiresOnEnter}` (no
  Ozone platform). Under `dbus-run-session` started outside the FHS env: the
  same 132/7. Inside `runtime-test` with a private headless scroll and a
  private bus, the whole suite: 139 OK, 0 SKIPPED, 0 FAILED, `SUCCESS: all
  tests passed.` in one launcher run, so the two display-needing seams
  (w2c, w3a) coexist in one batch. No coredump was written during the session
  (`coredumpctl list --since 03:25` empty) and no
  `views-shell-plugin-*.scope` was left over.
- **R1 run gate:** every `tree.json` of the 31 fresh runs (wave sequences,
  prove rows and the break attempts) holds 0 `"app_id": "views-shell"`; every
  `client.log` has only `get_layer_surface` (1 to 3) and, in the popup runs,
  `get_popup`; `get_toplevel` appears only in the `views_examples` controls and
  in the superseded no-flag runs, which draw nothing.
- **Production factory:** `ldd out/views/views_shell` names no
  `*test_support*` library (component: 324 libraries inside the FHS env, 313
  lines outside it with 34 system libraries not found there; release: 31);
  `gn desc … deps --all` on both out dirs: 0 `test_support`, 0 `//base/test`,
  0 `//ui/compositor:test_support`; the unittests target has 31, as it should.
  `gn path` to `//v8`, `//content`, both Blink renderer trees and `//chrome`:
  `No non-data paths` 5 of 5 on `out/views` and on `out/release`. The
  executable's `assert_no_deps` still lists `//ash/*`, `//chrome/*`,
  `//chromeos/*`, `//content/*`, `//gin/*`, `//third_party/blink/renderer/*`,
  `//v8/*`; the seams add their own (plugins also forbids `//ui/views/*` and
  `//views_shell/ui_tree/*`). No weakening since 604b6de.
- **Tree greps:** `git grep -nE '/home/[a-z]+'` over the tree (PROVE.md
  excluded) finds nothing; `TODO`/`FIXME`/`NOTIMPLEMENTED` under `shell/` are
  the five allowlisted upstream TODOs in `shell/tabs`, one in the F1 patch
  context, and no placeholder call; `placeholder` occurs only as the Textfield
  property name and in one comment; `tools/validate.sh` (fences, identity, 12
  manifests, 14 trees, 4 themes, invalid fixtures, 12 registry views, 14
  renders, no_stubs, three CONFORMANCE-OK) prints `VALIDATE-OK`;
  `tools/check-fences.sh` `fences: clean`; `python3 tools/prove-lint.py`
  `prove-lint ok (141 rows)`; `bash tools/theme-goldens.sh --check`
  `theme-goldens: ok (4 themes)`.
- **Screenshots:** every fresh capture was decoded (PNG filters undone, colours
  counted). A 6121-byte capture is the blank 1920x1080 black frame (1 colour):
  it appears only where expected (console probes, `views_examples`, the
  superseded no-flag runs). Bar runs are 6845–7083 bytes with 83–106 colours
  and a 2.9 % bar (white or the theme ground, the `0b57d0`/`70b8ff` pill);
  popup runs 12.0–12.4 KB, 287–316 colours; the assembled runs 11.1–14.3 KB,
  183–234 colours. The keyboard capture shows the `1` pill, the `03:56` clock,
  the `demo • now … hello from the bench` popup top right and `hello` in the
  centred field; the claude-light popup capture shows the menu in `#f9f9f7`.

## Refuted

1. **PR #30 (w2a), "Capability set reported on headless stock scroll: 22
   rows"** — overclaimed. `shell/wm/adapters/scroll/scroll_adapter.cc` declares
   `kScrollOnlyCapabilities` = `bindings.list`, `overview.toggle`, `scroll.jump`,
   `scroll.lua`, `scroll.overview`, `scroll.scroller`, `scroll.spaces`,
   `scroll.trails` from a static table, but `WmCommand::Kind`
   (`shell/wm/compositor_adapter.h`) has seven kinds (focus workspace, focus
   window, rename, move to workspace, toggle scratchpad, session exit, reload)
   and `CommandText()` spells only those. `wm_probe --dump` therefore reports
   22 capabilities of which 8 have no command, event or request behind them,
   and `PluginRegistry` (R14 gate, `PluginRegistryTest.RequiredCapabilityGatesRegistration`)
   registers a plugin that requires `scroll.trails` although nothing can serve
   it. The item's own result lists this; the PR body and
   `docs/compositor-adapters.md` "Real today" state the 22 as the capability
   set. The claim C14.3 (the dump's keys) still holds.
2. **P14.3's prove command** passes on stale evidence: `$R` is unset in its own
   shell, `bash /tools/bench/worker/headless.sh` fails, and the `;` lets the
   Python check read the previous run's `dump.json`. The claim is true (fresh
   seq: `STEP probe-dump rc 0`, keys printed); the row does not prove it.
   Corrected command in the table.
3. **PR #23 (w1b) release numbers as a statement about `views_shell`**: "PSS
   81.1 MB, 0% idle CPU, 30 threads, 55 FDs, runtime_deps 102 files / 208 MB,
   stripped 63,250,200" were measured on the chapter-1 test-factory binary
   (w1a had not merged). On today's main the release bar is 60.5 MB PSS, 17
   threads, 68 FDs, 61,177,544 B stripped, runtime_deps 14 files / 138,733,776
   B. `docs/bench/views-shell-154-release.md` dates its build, so the record
   is not false, but the PR's summary numbers no longer describe the program.
   The same holds for PR #24's four-way table (158.7 MB / 45 threads on the
   GPU path is 129.5 MB / 16 today) and for `views-shell-154-gpu.md`.

Not refuted after checking: the four byte-identical 6970-byte bar PNGs
(deterministic frame; eight bench captures share the md5); "no colour literal
and no pixel size" in `shell/ui_tree` (the two `SetPreferredSize` calls take
`LayoutProvider` metrics); "no test support remains" (D2's `ui_test_pak` is a
`copy()`, not testonly, and is declared); C11.1's explanation of the FD count
(323 shared objects pre-opened in the component build, 30 in release).

## Superseded

- **C3.4 / P3.4** (chapter 1): `views_shell` with no surface flag drew an
  `xdg_toplevel`. Since w2b (R1's second enforcement) it prints
  `views-shell draws only on layer surfaces (rule R1); pass --bar (or
  --left-tabs)` and exits 2: the driver's run, w2b's `prove-r1-exit-2` and
  `claim-C15.4` stages and `seq/w1a.sh`'s `plain` runs all show `rc=2`,
  `attaches: 0`. P3.4 now fails (rc 1) and will keep failing.
- **C9.3's no-flag half / PR #25's "C3.4 still holds"**: same change; the
  `seq/w1a.sh` `runs` stage now ends `fail=1` because of its `plain` and
  `plain-gpu` runs. The popup half still holds.
- **The chapter-1 and wave-1 footprint rows** (141.3 / 154.2 / 125.3 MB, 30
  threads, 344–345 FDs; release 81.1 MB / 55 FDs) describe the test-factory
  binary. The production numbers are below.
- **C15.2's 0.9976 ground fraction**: 0.9851 today, because the workspace
  strip's pill now sits in the bar's top 32 rows; the check (`> 0.9`) holds.

## Numbers today (main 7131e49, pixman compositor unless said, N as given)

| Row | PSS MB | threads | FDs | notes |
|---|---|---|---|---|
| component `--bar`, software compositing (w1a-bar, w1b-views-bar, w1c-fd, P9.2, P4.2) | 108.2–109.7 | 17 | 361 | N=5; attaches 2, colours 3; 0.0–0.2 % CPU |
| component `--bar --demo-popup` (w1a-popup, P4.3) | 110.6–110.9 | 17 | 363 | N=2 |
| component `--bar --gpu-compositing` (SwiftShader Vulkan, w1a-bar-gpu) | 131.2 | 35 | 372 | N=1 |
| component, gles2 compositor + SwiftShader client (w1c-gles2-ss) | 129.7 | 16 | 360 | N=1 |
| component, GPU-fair: gles2 compositor on renderD128, client default GL (w1c-gpu, P11.3) | 129.5 | 16 | 360 | N=2 |
| assembled `--bar --theme all-black` (w3c-plain) | 109.5 | 17 | 361 | N=1; the w3c record said 108.7 / 17 / 362 |
| assembled, GPU-fair (w3c-plain-gpu) | 129.2 | 16 | 360 | N=1; record 128.4 / 16 / 360 |
| assembled session runs (switch, notify, keyboard) | 113.1–114.3 | 19 | 370–372 | N=3; two extra threads are the scroll IPC and D-Bus threads |
| release `--bar` (w1b-release-bar, P10.3, w3c-release with the theme) | 59.7–60.5 | 17 | 68 | N=3 |
| `views_examples` control (never draws) | 160.7–161.5 | 61 | 24 | N=2, 202 ctxsw/s |

Release sizes: `views_shell` 86,922,976 B linked, 61,177,544 B stripped, ldd
31, `gn desc runtime_deps` 14 files / 138,733,776 B; `views_examples`
92,416,032 / 66,006,808, ldd 31, runtime_deps 102 files / 265,952,416 B.
Component `views_shell` 25,020,120 B, ldd 324, runtime_deps 346 files.

FD recount against w1c's explanation: of the 357 descriptors in the component
`views_shell` process, 323 are shared objects (276 from `out/views`, 51 system)
plus the executable, `icudtl.dat` and `ui_test.pak`; 28 are sockets, eventfds,
pipes, memfds and device nodes. The release process holds 64: 30 shared
objects plus the same files and 28 non-file descriptors. The count is
proportional to the mapped libraries, as w1c said.

## Findings for the synthesiser

- The prove commands that run `views_shell_unittests` in the FHS env (P9.4,
  P14.1, P20.1, P21.4, the w3b and w3c stages) inherit the bench user's live
  `DBUS_SESSION_BUS_ADDRESS`; with the whole suite that makes the four
  `NotificationServerBusTest` cases claim `org.freedesktop.Notifications` on
  that bus. Harmless on the headless bench, but R19's spirit is the runtime-test
  recipe of `seq/w2c.sh display`, which the driver used for the whole suite.
- `ShellContent::StartCompositor` resolves the socket from `$SCROLLSOCK`,
  `$SWAYSOCK` and `$I3SOCK` only, while `ScrollIpcClient` also tries `scroll
  --get-socketpath` and the `$XDG_RUNTIME_DIR` scan. With the variables unset
  `wm_probe` connects and the assembled shell runs without workspaces.
- Rows that only read a wave's stored log or rc (P3.1, P3.2, P4.1, P10.1,
  P15.1–P15.4, P16.1–P16.3, P19.1, P20.1's first half) pass whatever the tree
  does today. Future rows should run the stage they cite, as P21.x do.
- `seq/w1a.sh runs` fails by design now (its `plain` runs); `seq/w2a.sh record`
  recorded 76 frames against the committed 97 (both valid transcripts; the
  stage reports `differs` without failing).
- The unit-test tally (139 tests: style 22, bar 12, wm 18, notifications 18,
  ui_tree 26 with 14 golden instances, plugins 30) is the whole chapter-2
  suite; `shell/tabs` has no tests.

## Bench state left

- `~/chromium/src` at tag 154.0.8037.92, wired to this worktree
  (`~/views-shell-wt/w-verify-ch2` = main 7131e49): 28 changed paths, the five
  series patches applied; `wire.sh` resets it on the next use.
- `out/views`: args equal to `tools/bench/args.views.gn` (ninja, ccache),
  `ENSURE-OK views unchanged`; `views_shell` 25,020,120 B (03:42 CEST),
  `views_shell_unittests` (03:54), `views_examples`, `wm_probe`,
  `plugin_probe`, all from main.
- `out/release`: args equal to `tools/bench/args.release.gn`; `views_shell`
  86,922,976 B (03:57), `views_examples` 92,416,032 B.
- `out/views-t4` (7.4 GB, siso state, drifted args, a 36 MB chapter-1
  `views_shell`): untouched, still there.
- `~/views-shell-wt/`: main, spec-f1-v8-via-automation, t-views-shell-t2-bench,
  t-views-shell-t3-program, t-views-shell-t4-layer-shell,
  t-views-shell-t5-scroll-adapter, w-demo-left-strip-main, w-demo-left-tabs,
  w-tabstrip, w-tabstrip-vertical, w-w1a, w-w1b, w-w1c, w-w2a, w-w2b, w-w2c,
  w-w3a, w-w3b, w-w3c, w-verify-ch2 and two scripts; the chapter-1 copies
  carry the older `headless.sh` that P2.4, P3.4, P4.2 and P4.3 still call.
- `~/views-bench/results/`: every wave result directory rewritten by today's
  sequences (same names), plus `verify/` (factory, ensure, build and unittest
  logs), `verify-display/`, `verify-<break attempt>/`, `w1b-census/`;
  `~/views-bench/logs/verify-ch2.log`, `verify-ch2.pass1.log`,
  `verify-<item>.log` (w1b's full-sequence log was overwritten by its prove
  log; the census reports and run summaries survive under results).
  `~/views-bench/verify/` holds the driver and `main-sha`.
- `~/views-bench/bin/runtime-test-gpu` reinstalled by `seq/w1c.sh` and
  `seq/w3c.sh` (same file). No `vs-*` unit running; the bench lock was
  released at the end of the session.
- On the coordinator, the git worktrees t1 to t8, w1a to w3e, demo2,
  demo-left-tabs, lift-inventory, tabstrip and verify-ch2 exist; none was
  deleted. `w/w3e` (e54692c, never pushed, two agents in one worktree) is not on
  main: `examples/themes` on main holds the four example themes only.
