# The Chromium hop

A **hop** moves views-shell's pinned Chromium from one tag to the next. This page is the
process, and the release policy it serves. A hop is run by hand, by a person or an
agent, on the build host, within a compile budget Tom sets. It is never a timer and
never a CI job (rule R18). Cost is counted in iterations, never in time.

## Releases: none until v1, and v1 is Chrome's cue

Tom's ruling (2026-10-01):

- **No release history.** Before v1 there are no tags, no changelog, no version
  numbers and no compatibility promises. Tom daily-drives `main` on both seats;
  that is the test. `views-shell version` prints the commit.
- **Hops are by hand, when wanted.** Before v1, views-shell moves to a newer Chromium tag
  when Tom or an agent decides a hop is worth its iterations: a fix in `ui/`,
  `ui/ozone/` or Skia, an upstream change that retires a carried patch, or simply
  staying close to the Chrome Tom runs. There is no tracking promise (Stable or
  Extended) and no clock (rule R18). Each hop still follows the loop below and
  leaves a ledger.
- **v1 is cut when Google ships a Chrome release that Tom names as the signal to
  freeze.** views-shell then hops to that release's Chromium tag (the exact version string
  Chrome reports, for example `15x.0.xxxx.yy`), passes every gate, and that build
  becomes v1. The contracts freeze with it.

### What freezes at v1: the stable contracts

The idea that survives from the earlier stability notes is that stability lives in
the contracts, not in the C++:

1. **The contracts are stable from v1.** The plugin manifest, the `ui` tree, the T2
   plugin protocol, the CLI and its `--json` output, the native messaging verbs and
   the slot-file format do not break inside a major version after v1. Each carries
   its own `schemaVersion` and golden fixtures (`tools/validate.sh`). Before v1 they
   may change at any commit, and the goldens move with them.
2. **The base is a real Chrome release.** v1 is built on the exact Chromium tag of a
   Google Chrome release, which upstream still patches when it is cut.
3. **The release is reproducible.** v1 passed every gate and is reproducible from
   its pins: the Chromium tag, the carried patches, the ledger rows and the Nix
   derivation.

C++ internals (the five Ash ports, Views usage, the Ozone carry, `WmModel`) are
never a contract. They may change at every hop, before and after v1.

How views-shell moves after v1 (another Chrome release Tom names, or a cadence) is left
for v1 to decide (open decision P24).

## Bench policy: the newest Chrome stable at bench time

The bench (the headless build host driven only through
[`../tools/bench/`](../tools/bench/)) does not wait for a hop decision:

- **The bench tracks the newest Chrome stable at bench time.** When the bench is
  provisioned or re-provisioned, it fetches the newest Google Chrome Stable release
  available that day and pins its exact Chromium tag in
  `shell/build/CHROMIUM_VERSION`. Nothing is built against a tag the bench has not
  pinned, and every recorded run under `docs/bench/` names the tag it ran on.
- **A bench pin is not a release.** It carries no promise and no history (rule R27);
  it is simply the tree the chapter's evidence was produced on.
- **v1 is the Chrome release Tom names.** When he names one, the hop loop below moves
  the pin to that release's exact Chromium tag, the gates run, and the contracts
  freeze. Until then a hop happens only when someone decides it is worth its
  iterations.
- **Example, 2026-10-01:** the newest Chrome stable was `154.0.8037.92`, so the bench
  holds `154.0.8037.92` and the first hop of this lineage is a re-cut at that tag.

## Inputs

| File | Holds |
|---|---|
| `shell/build/CHROMIUM_VERSION` | the pinned tag (one line, for example `154.0.8037.92`) |
| `shell/patches/*.patch` | the carry, applied in the order listed in `shell/patches/series`, each with a header giving its purpose, anchors and last good tag |
| `ASH-PORT-LEDGER.md` | every ported Ash or Chromium file with its upstream revision |
| `docs/upgrade/LEDGER-<milestone>.md` | one ledger per hop (template below) |
| `docs/upgrade/` | the lifted agency protocol and the 150 ledger, as precedent |

## The loop

1. **Pin.** Choose the target tag: the Chromium version of the newest Google Chrome
   Stable release at bench time (from Chromium's release data, the version history API
   or `chromiumdash`), or, for v1, the exact version of the Chrome release Tom named.
   Record the tag, the Chrome release it corresponds to and the date in the new
   ledger. Fetch only that tag at depth 1, check it out, and let `gclient sync`
   reconcile DEPS with no `--revision`. A bare `gclient sync --revision` on a
   shallow clone triggers an unshallowing fetch that the server kills.
2. **Survey before compiling.** For every file a patch touches and every row of
   `ASH-PORT-LEDGER.md`, list the upstream commits between the old tag and the new
   one (`git log <old>..<new> -- <path>`). Write each one into the ledger with a
   verdict: `pull`, `skip` (with a reason), or `retire` (upstream now does what
   the carry did).
3. **Retire.** Delete each patch or port whose job upstream has taken over. Known
   candidate: the carried idle notifier, when upstream's arrives (expected at
   milestone 155 per the earlier ledger; verify, do not assume).
4. **Rebase.** Reset the tree to pristine before each authoritative wire. Apply
   every patch with plain `git apply`. A three-way merge is never used: on an
   already-wired tree it silently double-applies and leaves conflict markers.
   Classify each failure with the drift taxonomy (context drift, carry context,
   upstream refactor, structural) and fix it minimally. Generate hunks for shared
   files from the live tree with `difflib`, asserting the anchor matches exactly
   once.
5. **Mirror safety fixes.** The layer window is a sibling of `WaylandToplevelWindow`
   and `WaylandPopup`. Read the upstream diff of both, and mirror any lifetime,
   focus or configure-sequence fix into `wayland_layer_shell_window.cc`.
6. **Diff the ports.** For each ledger row (five today), diff the ported file
   against its new upstream revision. Pull or skip each hunk, and write the reason
   in the ledger. Update the row's revision. Then write a short drift note on the
   stock classes the kit names (`ui/views` controls, `ui/color` system ids,
   `ui/actions`, `ui::DialogModel`, `message_center` views, `MediaItemUIView`):
   a renamed or removed class is a kit change, not a port change.
7. **Build gate.** `gn gen` green, then `ninja -C out/views-shell views-shell`. Success is the
   literal link line for `./views-shell` in the log, a passing `no_stubs.py` scan, and
   `gn path out/views-shell //views_shell:views-shell //v8` printing nothing (rule R24).
   An exit code of zero alone is never success. Never disable a clang plugin, add
   a stub or suppress a warning to get past a real error. Check every API fix
   against the real headers of the new tag, not from memory.
8. **Unit gate.** The layer-shell unit tests on Chromium's test Wayland server with
   the layer-shell mock, and the adapter transcript tests.
9. **Run gate.** Under `runtime-test`, a nested headless scroll with a private
   runtime directory: every core surface maps with its namespace, the compositor's
   window list holds no views-shell window, typing reaches the launcher, and a rail click
   switches a workspace and is observed through the echo.
10. **Ledger and skill.** Finish the ledger. If the hop met a new kind of drift, add
    it to the hop skill, so the next hop knows it.
11. **Ship to the seats.** A static build as a Nix derivation, pinned to the tag,
    switched on by Tom. Before v1 this is not a release: no tag, no notes.

## Ledger template

```markdown
# Hop <old tag> → <new tag>

- Pinned: <new tag>, the Chromium version of Google Chrome <release> (<upstream release date>)
- Why this hop: <the reason, or "v1 freeze: Tom named this release">
- Build host: <host>
- Iterations to green: <count of build-gate attempts>

## Survey
| Path | Upstream commits | Verdict | Reason |
|---|---|---|---|

## Patches
| Patch | Applied strict? | Drift class | Fix |
|---|---|---|---|

## Ports (ASH-PORT-LEDGER rows touched)
| Ported file | Old revision | New revision | Hunks pulled | Hunks skipped, with reason |
|---|---|---|---|---|

## Retired
| Item | Upstream replacement |
|---|---|

## Gates
- Build: <literal link line>
- no_stubs: <literal result>
- Unit: <test summary line>
- Run: <evidence paths>

## New drift kinds (added to the hop skill)
```

The ledger records iterations and evidence. It never records durations.

## Where the carry stands

The last green Chromium in this lineage was 150.0.7871.124 (agency-mvp,
`ninja agency chrome`, 48,159 steps, 0 failed). No tree or binary survives, so
the first hop is a re-cut at the bench pin: the newest Chrome stable at bench time,
`154.0.8037.92` on 2026-10-01. The lifted patches are re-cut against that tag, never
required to apply from the lineage (Tom, 2026-10-01: "start from a newest chromium (as
per latest release) no need to 'apply cleanly'"), and the content-free shell main is
built for the first time. See [`../shell/PROVENANCE.md`](../shell/PROVENANCE.md)
for what is trimmed.
