#!/usr/bin/env bash
# Bench sequence for item w1b (chapter 2): restore out/views, build out/release, census, footprint.
# Runs on the bench OUTSIDE the FHS environment, as a transient user unit, under
# ~/views-bench/bench.lock (the caller takes the lock; see shell/build/README.md):
#   1. wire this worktree (stop unless the last line starts with WIRE-OK);
#   2. ensure-out views with tools/bench/args.views.gn, then build views_shell and
#      views_examples in out/views (restores the development directory for everyone);
#   3. ensure-out release with tools/bench/args.release.gn, then build both there;
#   4. census.sh for both binaries in both directories (results/w1b-census/);
#   5. three headless runs under the same harness: release views_shell --bar,
#      release views_examples (a non-drawing control, finding F2), and the
#      component views_shell --bar on the restored out/views.
# Every step echoes its rc; the script exits with the first failing build/wire rc,
# otherwise with the OR of the run rcs.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"; FHS="$B/build-env"; R="$B/results"
FLAGS="--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
step() { echo "=== w1b $1 $(date -u +%FT%TZ)"; }

step wire
wire_out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1); echo "$wire_out"
case "$(printf '%s\n' "$wire_out" | tail -1)" in WIRE-OK*) ;; *) echo "w1b: wire failed"; exit 1;; esac

step views-build
"$FHS" -c "cd ~/chromium/src && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn && autoninja -C out/views views_shell views_examples"
rc=$?; echo "views-build rc=$rc"; [ "$rc" = 0 ] || exit "$rc"

step release-build
"$FHS" -c "cd ~/chromium/src && mkdir -p out/release && bash $WT/tools/bench/worker/ensure-out.sh release $WT/tools/bench/args.release.gn && autoninja -C out/release views_shell views_examples"
rc=$?; echo "release-build rc=$rc"; [ "$rc" = 0 ] || exit "$rc"

step census
crc=0
for o in release views; do
  "$FHS" -c "bash $WT/tools/bench/worker/census.sh $o //views_shell:views_shell views_shell $R/w1b-census/$o-views_shell.txt" > /dev/null; r=$?; echo "census $o views_shell rc=$r"; crc=$((crc|r))
  "$FHS" -c "bash $WT/tools/bench/worker/census.sh $o //ui/views/examples:views_examples views_examples $R/w1b-census/$o-views_examples.txt" > /dev/null; r=$?; echo "census $o views_examples rc=$r"; crc=$((crc|r))
done
"$FHS" -c "cd ~/chromium/src && for t in //v8 //content //third_party/blink/renderer/core //third_party/blink/renderer/platform //chrome; do gn path out/release //views_shell:views_shell \$t; done" > "$R/w1b-census/release-gn-path.txt" 2>&1
echo "gn path no-browser: $(grep -c 'No non-data paths' "$R/w1b-census/release-gn-path.txt") of 5"

step runs
rm -rf "$R/w1b-release-bar" "$R/w1b-release-examples" "$R/w1b-views-bar"
bash "$WT/tools/bench/worker/headless.sh" "$R/w1b-release-bar" "$FHS" -c "~/chromium/src/out/release/views_shell $FLAGS --bar"; r1=$?; echo "run w1b-release-bar rc=$r1"
bash "$WT/tools/bench/worker/headless.sh" "$R/w1b-release-examples" "$FHS" -c "~/chromium/src/out/release/views_examples $FLAGS"; r2=$?; echo "run w1b-release-examples rc=$r2"
bash "$WT/tools/bench/worker/headless.sh" "$R/w1b-views-bar" "$FHS" -c "~/chromium/src/out/views/views_shell $FLAGS --bar"; r3=$?; echo "run w1b-views-bar rc=$r3"
step done
exit $((crc|r1|r2|r3))
