# CHROMIUM-UPGRADE.md — the LLM-assisted upgrade protocol

The repeatable discipline for riding near-bleeding-edge Chromium on the `//agency`
tier-2 fork. This is an **agent-in-the-loop process against the real compiler**
(the "second clock"), not a CI job — automating it prematurely hides the drift we
need to see. It gets cheaper each bump as the protocol sharpens; it does not
become a black box.

First exercised on **148.0.7760.0 → 149.0.7827.232** (see
[UPGRADE-LEDGER-149.md](UPGRADE-LEDGER-149.md) for that cycle's honest cost).

---

## 0. Shape of the fork (why this is tractable)

`//agency` is deliberately **additive tree + minimal patch set** (brave-style):

- An additive `src/agency/` source tree, reached from the root `gn_all` by one
  edge (`patches/agency-build-gate.patch`), plus per-feature additive files under
  `browser-features/*/files/`.
- A small set of **patches** to real Chromium files: `patches/*.patch` (5 agency
  patches) and `browser-features/*/patches/*.patch` (~112 across 14 wired
  features). Every patch is additive or a surgical edit — no forked files.

Upgrade cost is therefore concentrated in **re-applying patches** whose anchors
drift, plus a handful of genuine **API-drift** compile fixes. The bulk (stock
Chromium) just recompiles.

---

## 1. Sync the checkout (ds4 build server)

The ds4 checkout is **shallow** (`--no-history`) and the `.gclient` solution is
**`managed: False`** — both facts dictate the sync mechanics. Learned the hard
way this cycle: a bare `gclient sync --revision src@<TAG>` makes gclient run a
full `git fetch origin --no-tags` to resolve the tag, which on a shallow clone
of chromium/src means a multi-GB unshallowing fetch that googlesource **kills
with HTTP 502** (`fatal: expected 'packfile'`), on every retry, ~20 wasted GB.

The working sequence — fetch ONLY the new tag at depth 1, move `src` yourself,
then let gclient reconcile DEPS with **no `--revision` at all** (unmanaged src
is never touched):

```bash
ssh ds4-worker
cd ~/chromium/src
git fetch --depth 1 origin "refs/tags/<NEW_TAG>:refs/tags/<NEW_TAG>"
git checkout -f <NEW_TAG>          # -f: discards the previous cycle's applied mods
cd ~/chromium
gclient sync --nohooks --no-history -D -R -j2    # DEPS-only; src is managed:False
gclient runhooks -j4
```

Snapshot the outgoing tree first (`git diff > ~/agency-<OLD>-tree-mods-<date>.patch`)
— it is the ledger's baseline. Bump `build/CHROMIUM_VERSION` in the worktree to the
new tag. After checkout, `git status` should show only untracked overlay leftovers
(the rsynced `agency/`, feature new-files, cowl embedder); a stale untracked file
that the new cycle no longer overlays can linger and break the build — delete it
in-tree and record it in the ledger.

## 2. The wire gate (how patches get applied + tested)

Edit only in a **local worktree** (never on ds4). A gate script rsyncs it to ds4,
runs `build/wire-agency.sh` (agency tree + `agency-build-gate` + dbus visibility),
then `gn gen`. (agency-mvp carries the agency wire only; the `browser-features`
overlay/patch pass was dropped here — see DECISIONS.md.)

**Reset to pristine before every authoritative wire** (`git reset --hard HEAD`).
This is non-negotiable: the wire's `git apply --3way` fallback, run on an already-
wired tree, silently **double-applies** and can leave `<<<<<<< ours` conflict
markers in tracked files (they surfaced this cycle as a mojom parse error and a gn
`Replacing nonempty list`). A pristine tree each wire means every patch applies
**genuinely** — no "already applied" skip masks drift, and no accumulation cruft.

Success of this stage = **all patches apply strict** (grep the wire log for
`FAILED to apply` → none; and for `applied (--3way)` → ideally none) and **gn gen
green**.

## 3. Rebase drifted patches — the drift-class taxonomy

For each `FAILED to apply`, fetch the pristine target read-only
(`ssh ds4 'git show HEAD:<path>'` — safe even mid-build) and classify:

| Class | Symptom | Fix |
|---|---|---|
| **context-drift** | nearby upstream lines moved | re-anchor, recompute `@@` line numbers |
| **fork-context** | patch context cites *other* fork additions (cowl deps, another feature) absent from pristine | re-anchor on the nearest **stock** lines |
| **upstream-refactor** | the patched code was renamed/relocated/restructured | adapt the change minimally + correctly (a target moved, a header path moved, an enum table gained entries…) |
| **accumulated** | a shared chrome file (`chrome/browser/BUILD.gn`, `chrome_content_browser_client.cc`, `browser_prefs.cc`) that an **earlier wired feature already edited**, so the anchor doesn't exist on pristine | re-cut against the in-order accumulated tree (see §4) |
| **structural** | the dependency/target/API the patch needs no longer exists | product-level fix (vendor, port, or drop with justification) |

Guardrails: **never** disable a clang plugin, add a stub, or `// NOLINT` past a
real error; verify every API fix against the **real 149 headers over ssh**, not
from memory.

## 4. The empirical `difflib` technique (decisive for shared files)

Hand-computing `@@` offsets for patches that stack into the *same* function or
deps list as other features is where hand-rebasing keeps failing. Instead,
generate the patch from the live tree:

```python
# on ds4, against the current wired tree (all OTHER features applied, this one not)
before = read(path)
after  = before.replace(EXACT_ANCHOR, EXACT_ANCHOR + INSERTION, 1)   # assert count==1
diff   = difflib.unified_diff(before.splitlines(keepends=True),
                              after.splitlines(keepends=True),
                              fromfile="a/"+path, tofile="b/"+path, n=3)
```

The `count==1` assert catches content drift; `difflib` computes correct hunks and
whitespace. For two features that edit the *same* function (this cycle: the
web-request throttle hook — url-privacy, agent-provenance, adblock all push into
`CreateURLLoaderThrottles`), generate the **later** feature's patch against a tree
that **already has the earlier feature applied**, so both apply strict in wire
order. Copy the generated patch back into the worktree.

**Do NOT mutate the ds4 tree while a build is running.** Read-only `git show
HEAD:<path>` is fine; `git apply` / `reset` / wire will corrupt an in-flight ninja.
Overlap patch-rebasing (offline, in the worktree) with the long stock-Chromium
compile; do the authoritative re-wire only when ninja is paused/done.

## 5. Compile to green (batch the API drift)

Launch the big build **detached with `-k 0`** so one pass harvests *all* errors
rather than stopping at the first:

```bash
setsid nohup nice -n10 ninja -k 0 -C out/agency agency chrome >/tmp/ninja.log 2>&1 </dev/null &
```

Poll (`grep -oE "\[[0-9]+/[0-9]+\]" | tail -1`; `grep FAILED:`). Fix each error
class in a batch, re-wire, relaunch incrementally (stock objects stay cached).
Recurring drift surfaces every bump:

- **//dbus** — `ScopedDBusError` / `CallMethodAndBlockWithErrorDetails` gone;
  `CallMethodAndBlock` returns `base::expected<…, dbus::Error>` (`.has_value()`).
- **base::Value** — `Dict`/`List` → `base::DictValue`/`base::ListValue`.
- **clang memory-safety plugins** — `raw_ptr<T>`, `raw_ref<T>`, out-of-line
  ctor/dtor (chromium-style), `UNSAFE_BUFFERS`/`base::span`.
- **GN visibility allowlists** — restricted targets need `//agency/*` (or the
  agency subtarget) added; these are hard **gn-gen** blockers.
- **assert(is_chromeos) walls** — anything reaching `//ash`.
- **Wayland/Ozone weld** — the most fragile carry (layer-shell; cowl workstream).

## 6. Verify (never trust "ninja exited 0" alone)

1. Detached build ends with `LINK ./chrome` and `pgrep -x ninja` shows exited.
2. Fresh **incremental** `ninja -C out/agency agency chrome` → `ninja: no work to
   do`, exit 0 (proves the tree is self-consistent, no mtime staleness).
3. `python3 tools/no_stubs.py` (libclang false-positives are known).
4. Update the per-bump ledger honestly.

---

## 148 → 149 deltas (this cycle)

- **Biggest refactor:** `components/os_crypt/sync` **deleted** wholesale (libsecret
  key storage → async D-Bus `freedesktop_secret_key_provider`). The importer's
  decryptor reused its `LibsecretLoader`; fix = vendor the small loader into
  `//agency/browser/importer` (drop the dep, drop the obsolete visibility patch).
- **Dominant drift pattern:** new upstream subsystems (`accessibility_annotator*`,
  `ai_suggestions`, `indigo`, `device_reauth`) inserted entries into the very
  alphabetical allowlists / pref-registration lists / deps lists / enum tables the
  fork also appends to. Mostly "a new neighbour moved my anchor," rarely semantic.
- **New stock `WebPreferences::is_indigo_onboarding` field** rippled through the
  mojom + struct + traits (4 files) the url-privacy GPC field also touches.
- **Histogram enum collision:** 149 claimed the exact `extension_function_
  histogram_value.h` slots the fork used — renumber the fork block above the new
  stock tail.
- **26** browser-feature patches + `agency-build-gate` + `lit-visibility` needed
  rebasing; **0** needed `--3way` in the final wire. See the ledger for the table.
