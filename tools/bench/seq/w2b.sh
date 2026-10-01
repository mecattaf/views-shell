#!/usr/bin/env bash
# w2b's whole bench sequence (chapter 2: the style kit, the bar with its
# clock, SurfaceSpec and the R1 check). Runs on the bench, outside the FHS
# environment (runtime-test must not nest inside it), under
# ~/views-bench/bench.lock, as a transient user unit: see tools/bench/README.md.
# Stages, all by default, or the ones named:
#   wire    pristine checkout, this worktree's shell/ as src/views_shell, the
#           data mirror (style/, examples/ ...), the patches
#   build   tools/bench/worker/ensure-out.sh views, gn check of //views_shell,
#           build views_shell and views_shell_unittests
#   tests   views_shell_unittests --gtest_filter=Theme*:Bar*:Clock*
#   runs    headless --bar --theme <claude-dark|all-black|noir>, --bar without a
#           theme, --bar --demo-popup with claude-light, --left-tabs with noir
#   prove   the claims' checks on those runs (bar ground colour, the R1 exit
#           code), against the binary built in this same lock hold
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
RT="$HOME/.local/bin/runtime-test"
STAGES="${*:-wire build tests runs prove}"
GL="--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
THEMES="$WT/examples/themes"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }

echo "w2b-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1)
  echo "$out"
  case "$(printf '%s\n' "$out" | tail -n 1)" in
    WIRE-OK*) step wire 0;;
    *) step wire 1; echo "w2b-seq: wire failed, stopping"; exit 1;;
  esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step ensure-out $rc
  [ $rc = 0 ] || { echo "w2b-seq: ensure-out failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -C out/views views_shell views_shell_unittests"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w2b-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests"
  echo "generated table: $(grep -c 'ui::kColorSys' "$SRC/out/views/gen/views_shell/style/theme_map_table.cc") pins"
fi

if has tests; then
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='Theme*:Bar*:Clock*'" > "$RES/w2b-unittests.log" 2>&1
  rc=$?; step unittests $rc
  grep -E '^\[ *(RUN|OK|FAILED|PASSED) *\]|tests? ran|Failure|Expected|Which is|actual' "$RES/w2b-unittests.log" | head -n 120
fi

run() {  # run <name> <flags...>
  local name="$1"; shift
  bash "$WT/tools/bench/worker/headless.sh" "$RES/w2b-$name" "$FHS" -c "~/chromium/src/out/views/views_shell $GL $*"
  local rc=$?
  step "run-$name" $rc
  grep -E 'theme:|left-tabs:|FATAL|ERROR|Check failed|^#[0-9]+ ' "$RES/w2b-$name/client.log" | head -n 20
  grep -E 'attaches|colours|alive|get_layer_surface|get_popup|get_toplevel|PSS' "$RES/w2b-$name/summary.txt" | sort -u
}

if has runs; then
  run claude-dark --bar --theme "$THEMES/claude-dark"
  run all-black --bar --theme "$THEMES/all-black"
  run noir --bar --theme "$THEMES/noir"
  run plain --bar
  run light-popup --bar --demo-popup --theme "$THEMES/claude-light"
  run left-tabs --left-tabs --theme "$THEMES/noir"
fi

# bar_ground <run> <theme>: the fraction of sampled pixels in the top 32 rows
# that are exactly the theme's resolved background; > 0.9 passes.
bar_ground() {
  python3 - "$RES/w2b-$1/shot.ppm" "$THEMES/$2/colors.toml" <<'PY'
import re, sys
d = open(sys.argv[1], 'rb').read()
parts = d.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
m = re.search(r'^background\s*=\s*"#([0-9A-Fa-f]{6})"', open(sys.argv[2]).read(), re.M)
bg = bytes.fromhex(m.group(1))
xs = range(0, w, 7)
n = sum(1 for y in range(32) for x in xs if px[3 * (y * w + x):3 * (y * w + x) + 3] == bg)
total = 32 * len(xs)
print('bar-bg-fraction', m.group(1), n / total)
sys.exit(0 if n / total > 0.9 else 1)
PY
}

if has prove; then
  bar_ground claude-dark claude-dark; step prove-claude-dark-ground $?
  bar_ground all-black all-black; step prove-all-black-ground $?
  bar_ground noir noir; step prove-noir-ground $?
  bar_ground light-popup claude-light; step prove-claude-light-ground $?
  grep -qE "attaches: [1-9]" "$RES/w2b-plain/summary.txt" && grep -qE "colours: ([2-9]|[1-9][0-9]+)" "$RES/w2b-plain/summary.txt"
  step prove-plain-draws $?
  grep -q get_popup "$RES/w2b-light-popup/summary.txt"; step prove-popup $?
  grep -q "get_toplevel" "$RES"/w2b-*/summary.txt; [ $? = 1 ]; step prove-no-toplevel $?
  # Rule R1: no surface flag, no window. Under runtime-test like every run of
  # the binary; it exits before connecting to any compositor.
  "$RT" -- "$FHS" -c "~/chromium/src/out/views/views_shell $GL --run-for-seconds=2" > "$RES/w2b-no-surface.log" 2>&1
  rc=$?; echo "no-surface rc=$rc"; grep -o 'views-shell draws only on layer surfaces.*' "$RES/w2b-no-surface.log"
  [ $rc = 2 ]; step prove-r1-exit-2 $?
  "$RT" -- "$FHS" -c "~/chromium/src/out/views/views_shell $GL --bar --theme /nonexistent" > "$RES/w2b-bad-theme.log" 2>&1
  rc=$?; echo "bad-theme rc=$rc"; grep -o -- '--theme: .*' "$RES/w2b-bad-theme.log"
  [ $rc = 2 ]; step prove-bad-theme-exit-2 $?
fi

echo "w2b-seq: done, fail=$fail"
exit $fail
