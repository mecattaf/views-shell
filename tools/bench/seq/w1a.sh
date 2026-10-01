#!/usr/bin/env bash
# w1a's whole bench sequence (chapter 2, T9: the production in-process viz
# host). Runs on the bench, outside the FHS environment (runtime-test must not
# nest inside it), under ~/views-bench/bench.lock, as a transient user unit:
# see tools/bench/README.md. Stages, all by default, or the ones named:
#   wire    pristine checkout, this worktree's shell/ as src/views_shell, patches
#   build   restore out/views to tools/bench/args.views.gn (gn clean when the
#           args drifted or siso state is present), gn gen, gn check, build
#           views_shell and views_shell_unittests
#   graph   gn desc / gn path evidence for C9.1
#   runs    headless runs: --bar, --bar --demo-popup, no flags (software,
#           the default), then --bar and no flags with --gpu-compositing
#   tests   views_shell_unittests
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
STAGES="${*:-wire build graph runs tests}"
GL="--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }

echo "w1a-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1); rc=$?
  echo "$out"
  last=$(printf '%s\n' "$out" | tail -n 1)
  case "$last" in WIRE-OK*) step wire 0;; *) step wire 1; echo "w1a-seq: wire failed, stopping"; exit 1;; esac
fi

if has build; then
  "$FHS" -c "cd $SRC && if ! cmp -s $WT/tools/bench/args.views.gn out/views/args.gn || [ -e out/views/.siso_deps ]; then echo 'out/views: restoring args.gn and cleaning'; cp $WT/tools/bench/args.views.gn out/views/args.gn && gn clean out/views; fi && gn gen out/views"
  rc=$?; step gn-gen $rc
  [ $rc = 0 ] || { echo "w1a-seq: gn gen failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -C out/views views_shell views_shell_unittests"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w1a-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests"
fi

if has graph; then
  "$FHS" -c "cd $SRC && gn desc out/views //views_shell:views_shell testonly && gn desc out/views //views_shell:views_shell deps --all" > "$RES/w1a-graph-desc.txt" 2>&1
  step gn-desc $?
  echo "testonly: $(head -n 1 "$RES/w1a-graph-desc.txt"); test_support lines: $(grep -c test_support "$RES/w1a-graph-desc.txt")"
  "$FHS" -c "cd $SRC && for t in //v8 //content //third_party/blink/renderer/core //third_party/blink/renderer/platform //chrome; do gn path out/views //views_shell:views_shell \$t; done" > "$RES/w1a-graph-path.txt" 2>&1
  step gn-path $?
  echo "No non-data paths: $(grep -c 'No non-data paths' "$RES/w1a-graph-path.txt") of 5"
fi

run() {  # run <name> <flags...>
  local name="$1"; shift
  bash "$WT/tools/bench/worker/headless.sh" "$RES/w1a-$name" "$FHS" -c "~/chromium/src/out/views/views_shell $GL $*"
  local rc=$?
  step "run-$name" $rc
  cat "$RES/w1a-$name/summary.txt"
  grep -E 'in-process viz up|ack_configure|FATAL|ERROR' "$RES/w1a-$name/client.log" | grep -v '^\[.*wl_' | head -n 20
  echo "ack_configure: $(grep -cE 'ack_configure' "$RES/w1a-$name/client.log")"
}

if has runs; then
  run bar --bar
  run popup --bar --demo-popup
  run plain
  run bar-gpu --bar --gpu-compositing
  run plain-gpu --gpu-compositing
fi

if has tests; then
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests" > "$RES/w1a-unittests.log" 2>&1
  rc=$?; step unittests $rc
  tail -n 15 "$RES/w1a-unittests.log"
fi

echo "w1a-seq: done, fail=$fail"
exit $fail
