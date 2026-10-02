#!/usr/bin/env bash
# w2a's whole bench sequence (chapter 2: the scroll adapter, WmModel and
# wm_probe). Runs on the bench, outside the FHS environment (runtime-test must
# not nest inside it), under ~/views-bench/bench.lock, as a transient user
# unit: see tools/bench/README.md. Stages, all by default, or the ones named:
#   wire     pristine checkout, this worktree's shell/ as src/views_shell, patches
#   build    ensure-out.sh (args restore, gn gen), gn check of //views_shell/wm,
#            build views_shell, views_shell_unittests and wm_probe
#   record   re-record testdata/scroll-transcript.jsonl from a headless scroll,
#            with the new views_shell as the window (results/w2a-record)
#   tests    views_shell_unittests, all, then Scroll*:WmModel*
#   probes   wm_probe --dump (bare, and with a window), --switch 3, a timeout
#            run against a missing socket, and --watch around two switches
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
STAGES="${*:-wire build record tests probes}"
GL="--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
PROBE="~/chromium/src/out/views/wm_probe"
SHELL_BIN="~/chromium/src/out/views/views_shell"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }

echo "w2a-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1)
  echo "$out"
  case "$(printf '%s\n' "$out" | tail -n 1)" in
    WIRE-OK*) step wire 0;;
    *) step wire 1; echo "w2a-seq: wire failed, stopping"; exit 1;;
  esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step ensure-out $rc
  [ $rc = 0 ] || { echo "w2a-seq: ensure-out failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/wm/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -C out/views views_shell views_shell_unittests views_shell/wm:wm_probe"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w2a-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests" "$SRC/out/views/wm_probe"
fi

if has record; then
  # The window is views_shell with no flags (one xdg_toplevel). The recorder
  # waits for it, then drives the adapter's command spellings and ends with exit.
  bash "$WT/tools/bench/worker/headless.sh" "$RES/w2a-record" "$FHS" -c \
    "python3 $WT/shell/wm/adapters/scroll/testdata/record_transcript.py --out $RES/w2a-record/transcript.jsonl -- $SHELL_BIN $GL"
  grep -E '^RECORD' "$RES/w2a-record/client.log"
  if grep -q '^RECORD-DONE' "$RES/w2a-record/client.log" && ! grep -qE '/home/[a-z]+' "$RES/w2a-record/transcript.jsonl"; then
    step record 0
    if cmp -s "$RES/w2a-record/transcript.jsonl" "$WT/shell/wm/adapters/scroll/testdata/scroll-transcript.jsonl"; then
      echo "record: identical to the committed transcript"
    else
      echo "record: differs from the committed transcript ($(wc -l < "$RES/w2a-record/transcript.jsonl") frames recorded)"
    fi
  else
    step record 1
  fi
fi

if has tests; then
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests" > "$RES/w2a-unittests.log" 2>&1
  rc=$?; step unittests $rc
  grep -E '^\[ *(OK|FAILED|PASSED|RUN) *\]|tests? (ran|passed)|SUCCESS|FAILURE' "$RES/w2a-unittests.log" | tail -n 60
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='Scroll*:WmModel*'" > "$RES/w2a-unittests-filtered.log" 2>&1
  rc=$?; step unittests-filtered $rc
  echo "OK lines: $(grep -c '^\[       OK \]' "$RES/w2a-unittests-filtered.log")"
fi

probe() {  # probe <name> <expected rc> <client command>
  local name="$1" want="$2"; shift 2
  bash "$WT/tools/bench/worker/headless.sh" "$RES/w2a-$name" "$FHS" -c "$*"
  echo "headless.sh rc $? (a console client is not alive at capture; the log is the evidence)"
  grep -vE '^\[views-shell-build-env\]|^  (depot_tools|ccache):' "$RES/w2a-$name/client.log" | head -n 60
  local rc
  rc=$(grep -oE '^PROBE-RC [0-9]+' "$RES/w2a-$name/client.log" | tail -n 1 | cut -d' ' -f2)
  [ "$rc" = "$want" ]; step "probe-$name (rc ${rc:-none}, want $want)" $?
}

if has probes; then
  probe dump 0 "$PROBE --dump | tee $RES/w2a-dump/dump.json; echo PROBE-RC \${PIPESTATUS[0]}"
  python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print('dump keys:', sorted(d)); assert {'outputs','workspaces','windows','mru'} <= d.keys()" "$RES/w2a-dump/dump.json"
  step dump-json $?
  probe switch 0 "$PROBE --switch 3 --timeout-seconds 20; echo PROBE-RC \$?"
  grep -q 'ECHO workspace 3' "$RES/w2a-switch/client.log" && grep -q '"name": "3"' "$RES/w2a-switch/tree.json"
  step switch-echo-and-tree $?
  probe timeout 1 "SCROLLSOCK=/nonexistent $PROBE --switch 3 --timeout-seconds 3; echo PROBE-RC \$?"
  # With a window: views_shell (no flags) maps one toplevel; its output goes to
  # window.log, so headless.sh sees no attach in client.log and captures after
  # its attach wait (about 15 s), by when these sequences have finished.
  probe dump-window 0 "$SHELL_BIN $GL > $RES/w2a-dump-window/window.log 2>&1 & sleep 7; $PROBE --dump | tee $RES/w2a-dump-window/dump.json; echo PROBE-RC \${PIPESTATUS[0]}"
  probe watch 0 "$SHELL_BIN $GL > $RES/w2a-watch/window.log 2>&1 & sleep 5; ( sleep 2; $PROBE --switch 3; sleep 1; $PROBE --switch 1 ) & $PROBE --watch 7; echo PROBE-RC \$?"
fi

echo "w2a-seq: done, fail=$fail"
exit $fail
