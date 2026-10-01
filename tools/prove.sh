#!/usr/bin/env bash
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Re-run the proofs recorded in PROVE.md from this checkout.
#
#   tools/prove.sh [--ids <regex>] [--form local|bench] [--latest] [--dry-run]
#                  [--timeout <seconds>] [--no-toolbox] [-v]
#
# Rows come from tools/prove-lint.py's parser (one parser for the lint and the
# runner). A table line the parser cannot read stops the run (exit 2): run
# `python3 tools/prove-lint.py` to see which. Selection, in this order:
#   --latest     keep only the newest row per id (newest `at`, then the later
#                line): PROVE.md is append-only and re-verification repeats ids
#   --ids RE     keep rows whose id matches the Python regex RE (re.search)
#   --form F     keep rows whose effective form is F. A row is bench when it is
#                recorded as bench or its command reaches the bench (an `ssh`,
#                or tools/bench/sync.sh, job.sh or lock.sh); every other row is
#                local. Several chapter-1 and wave-1 rows say `local` but ssh.
#
# Each selected row's command (the backticked span, `\|` and `\`` unescaped) runs
# from the repository root as `bash -c <command>`, stdin /dev/null, in its own
# process group, killed after --timeout seconds (default 1800; rc 124). A row
# passes when its command exits 0 now; a row recorded as `fail` is expected to
# fail and is reported as fail, so use --latest for the current state. A row
# whose command cell is prose (a legacy exception in tools/prove-lint.py) is not
# run: it prints `<id> - manual 0` and is listed on a PROVE-MANUAL line. Inside
# a row that runs tools/prove.sh itself (PROVE_SH_NESTED=1 in its environment),
# rows that run tools/prove.sh are not run either (`<id> - nested 0`, listed on
# a PROVE-NESTED line), so a row such as P18.1 does not recurse.
#
# Output: one line per row, `<id> <rc> <pass|fail> <seconds>`, then
# `PROVE-RUN <passed>/<total>` (manual and nested rows are not in the total).
# -v also prints each row's output, indented. prove-results.tsv (gitignored) at
# the repository root, written by the outermost run only, gets one line per row: id, rc, result, seconds and the first
# 200 bytes of the combined output, with tab, newline and backslash escaped.
# Exit 0 when every run row passed, 1 otherwise, 2 on a usage or parse error.
#
# --dry-run prints `<id> [<form>] <command>` per selected row (and `<id> [<form>]
# MANUAL: <cell>` for prose rows), marks a command that takes the bench lock
# itself with `[takes bench.lock]`, runs nothing, writes nothing and exits 0.
#
# Toolbox: the rows assume python3 with jsonschema (tools/validate.sh, the
# conformance runner) and node (extension/sync-probe.js). When the local python3
# lacks jsonschema or node is missing and nix is available, the runner builds
# both from the nixpkgs flake once and puts their bin directories first on PATH
# for every row. --no-toolbox skips this.
#
# Bench rows are run, not wrapped: this script never ssh-es by itself and never
# takes ~/views-bench/bench.lock. A verifier holds the lock across many bench
# rows with tools/bench/lock.sh hold ... release (see tools/bench/README.md);
# rows marked [takes bench.lock] then wait for it, so run those outside a hold.
set -uo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 2

dry=0 toolbox=1
for a in "$@"; do
  case "$a" in
    --dry-run) dry=1 ;;
    --no-toolbox) toolbox=0 ;;
    -h|--help) awk 'NR > 4 && /^#/ { sub(/^# ?/, ""); print; next } NR > 4 { exit }' "$0"; exit 0 ;;
  esac
done

if [ "$dry" = 0 ] && [ "$toolbox" = 1 ] && command -v nix >/dev/null 2>&1 &&
   { ! python3 -c 'import jsonschema' 2>/dev/null || ! command -v node >/dev/null 2>&1; }; then
  if paths=$(nix build --no-link --print-out-paths --impure --expr \
      'let p = (builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem};
       in [ (p.python3.withPackages (ps: [ ps.jsonschema ])) p.nodejs ]' 2>/dev/null); then
    for p in $paths; do PATH="$p/bin:$PATH"; done
    export PATH
  else
    echo "prove: toolbox build failed; rows run with the local PATH" >&2
  fi
fi

exec python3 - "$root" "$@" <<'PY'
import argparse, importlib.util, os, pathlib, re, signal, subprocess, sys, time

root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("prove_lint", root / "tools/prove-lint.py")
lint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lint)

ap = argparse.ArgumentParser(prog="tools/prove.sh")
ap.add_argument("--ids")
ap.add_argument("--form", choices=["local", "bench"])
ap.add_argument("--latest", action="store_true")
ap.add_argument("--dry-run", action="store_true")
ap.add_argument("--timeout", type=int, default=1800)
ap.add_argument("--no-toolbox", action="store_true")
ap.add_argument("-v", action="store_true")
args = ap.parse_args(sys.argv[2:])

rows = lint.read_rows(root / "PROVE.md")
broken = [r for r in rows if not r.ok_shape]
if broken:
    for r in broken:
        print(f"prove: PROVE.md line {r.line}: {len(r.cells)} cells, expected 10", file=sys.stderr)
    print("prove: PROVE.md does not parse; run python3 tools/prove-lint.py", file=sys.stderr)
    sys.exit(2)

if args.latest:
    newest = {}
    for r in rows:
        if r.id not in newest or r.sort_key > newest[r.id].sort_key:
            newest[r.id] = r
    rows = [r for r in rows if newest[r.id] is r]
if args.ids:
    try:
        rx = re.compile(args.ids)
    except re.error as e:
        print(f"prove: --ids: {e}", file=sys.stderr)
        sys.exit(2)
    rows = [r for r in rows if rx.search(r.id)]
if args.form:
    rows = [r for r in rows if r.effective_form == args.form]
rows.sort(key=lambda r: (lint.id_order(r.id), r.line))

if args.dry_run:
    for r in rows:
        if r.kind == "run":
            lock = " [takes bench.lock]" if "bench.lock" in r.command else ""
            print(f"{r.id} [{r.effective_form}]{lock} {r.command}")
        else:
            print(f"{r.id} [{r.effective_form}] MANUAL: {r.command_cell}")
    print(f"PROVE-DRY {len(rows)} rows")
    sys.exit(0)

def esc(b):
    s = b[:200].decode("utf-8", "replace")
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")

passed = total = 0
manual, nested = [], []
# A row whose command runs tools/prove.sh (P18.1) would recurse forever: inside
# a run, such rows are reported as nested and not run.
inside = os.environ.get("PROVE_SH_NESTED") == "1"
child_env = dict(os.environ, PROVE_SH_NESTED="1")
# Only the outermost run writes prove-results.tsv: a nested run would truncate
# the file its parent is writing.
with open(os.devnull if inside else root / "prove-results.tsv", "w") as tsv:
    tsv.write("id\trc\tresult\tseconds\toutput\n")
    for r in rows:
        if r.kind != "run":
            manual.append(r.id)
            print(f"{r.id} - manual 0", flush=True)
            tsv.write(f"{r.id}\t-\tmanual\t0\t\n")
            continue
        if inside and "tools/prove.sh" in r.command:
            nested.append(r.id)
            print(f"{r.id} - nested 0", flush=True)
            tsv.write(f"{r.id}\t-\tnested\t0\t\n")
            continue
        total += 1
        t0 = time.monotonic()
        p = subprocess.Popen(["bash", "-c", r.command], cwd=root,
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, start_new_session=True,
                             env=child_env)
        try:
            out, _ = p.communicate(timeout=args.timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            out, _ = p.communicate()
            out += f"\nprove: timed out after {args.timeout} s\n".encode()
            rc = 124
        secs = time.monotonic() - t0
        ok = rc == 0
        passed += ok
        res = "pass" if ok else "fail"
        print(f"{r.id} {rc} {res} {secs:.1f}", flush=True)
        if args.v:
            for line in out.decode("utf-8", "replace").splitlines():
                print(f"    {line}")
        tsv.write(f"{r.id}\t{rc}\t{res}\t{secs:.1f}\t{esc(out)}\n")
if manual:
    print(f"PROVE-MANUAL {len(manual)}: {' '.join(manual)}")
if nested:
    print(f"PROVE-NESTED {len(nested)}: {' '.join(nested)}")
print(f"PROVE-RUN {passed}/{total}")
sys.exit(0 if passed == total else 1)
PY
