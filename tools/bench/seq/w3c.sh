#!/usr/bin/env bash
# w3c's whole bench sequence (chapter 2 assembly: the bar's workspace strip on
# the WmModel, notification popups as layer surfaces, the keyboard probe on an
# exclusive overlay surface). Runs on the bench, outside the FHS environment
# (runtime-test must not nest inside it), under ~/views-bench/bench.lock, as a
# transient user unit: see tools/bench/README.md.
# Stages, all by default, or the ones named:
#   wire     pristine checkout, this worktree's shell/ as src/views_shell, patches
#   build    ensure-out.sh views, gn check of //views_shell, build views_shell
#            and views_shell_unittests, run WorkspaceStrip* and Bar*
#   runs     headless runs through worker/session-demo.sh: switch
#            (--demo-workspace-switch), notify (notify-send fires), keyboard
#            (--demo-keyboard, wtype fires), exit (--run-for-seconds=12, the
#            exit code of an orderly shutdown); then the footprint rows of the
#            plain assembled --bar run, CLIENT_FD_DUMP=1: software (pixman
#            compositor, F2 switches) and the GPU-fair variant of w1c
#   claims   the PR's prove commands for C21.1, C21.2, C21.3 and C21.4,
#            literally, in the same lock hold that built the binary
#   release  the same assembled --bar on out/release (the real footprint)
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
STAGES="${*:-wire build runs claims release}"
GL="--ozone-platform=wayland --use-gl=angle --use-angle=swiftshader"
THEME="$WT/examples/themes/all-black"
DEMO="$WT/tools/bench/worker/session-demo.sh"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }

echo "w3c-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1)
  echo "$out"
  case "$(printf '%s\n' "$out" | tail -n 1)" in
    WIRE-OK*) step wire 0;;
    *) step wire 1; echo "w3c-seq: wire failed, stopping"; exit 1;;
  esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step ensure-out $rc
  [ $rc = 0 ] || { echo "w3c-seq: ensure-out failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -k 0 -C out/views views_shell views_shell_unittests"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w3c-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests"
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='WorkspaceStrip*:Bar*:Clock*'" > "$RES/w3c-unittests.log" 2>&1
  rc=$?; step unittests $rc
  grep -E '^\[ *(RUN|OK|FAILED|PASSED) *\]|tests? ran|Failure|Expected|Which is|actual|Check failed' "$RES/w3c-unittests.log" | head -n 80
fi

# run <name> <flags...>: views_shell with the F2 switches and the all-black
# theme, driven by session-demo.sh.
run() {
  local name="$1"; shift
  bash "$WT/tools/bench/worker/headless.sh" "$RES/w3c-$name" "$FHS" -c "bash $DEMO $SRC/out/views/views_shell $GL --bar --theme $THEME $*"
  local rc=$?
  step "run-$name" $rc
  grep -E 'session-demo:|wm:|demo-workspace-switch|ECHO|notifications:|keyboard-probe|TYPED|FATAL|ERROR|Check failed' "$RES/w3c-$name/client.log" | grep -v '^\[.*\] *->' | head -n 40
  grep -E 'wl_keyboard[#@][0-9]+\.(enter|leave|keymap)|zwp_virtual|get_layer_surface\(' "$RES/w3c-$name/client.log" | head -n 12
  grep -E 'first attach|attaches|colours|alive|get_layer_surface|get_popup|get_toplevel|PSS|fd kinds' "$RES/w3c-$name/summary.txt" | sort -u
}

if has runs; then
  run switch --demo-workspace-switch
  run notify
  run keyboard --demo-keyboard
  # The exit row: --run-for-seconds ends the program inside the 10 s measure
  # window, so session-demo prints the exit code of the orderly teardown.
  CLIENT_SETTLE=2 run exit --run-for-seconds=12
  grep -o 'session-demo: views_shell exited rc [0-9]*' "$RES/w3c-exit/client.log"
  grep -q 'session-demo: views_shell exited rc 0' "$RES/w3c-exit/client.log"; step exit-rc-0 $?

  # Footprint of the plain assembled program (adapter, model, notifications,
  # bar): the software row, then the GPU-fair row of w1c (render node bound,
  # gles2 compositor, the client's default GL).
  CLIENT_FD_DUMP=1 bash "$WT/tools/bench/worker/headless.sh" "$RES/w3c-plain" "$FHS" -c "$SRC/out/views/views_shell $GL --bar --theme $THEME"
  step run-plain $?
  grep -E 'wm:|notifications:|FATAL|Check failed' "$RES/w3c-plain/client.log" | head
  grep -E 'first attach|attaches|colours|alive|PSS|fd kinds' "$RES/w3c-plain/summary.txt" | sort -u
  mkdir -p "$B/bin" && cp "$WT/tools/bench/worker/runtime-test-gpu" "$B/bin/runtime-test-gpu" && chmod +x "$B/bin/runtime-test-gpu"
  CLIENT_FD_DUMP=1 BENCH_RUNTIME_TEST="$B/bin/runtime-test-gpu" BENCH_RUNTIME_TEST_ARGS=--allow-dri \
    BENCH_WLR_RENDERER=gles2 WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 \
    bash "$WT/tools/bench/worker/headless.sh" "$RES/w3c-plain-gpu" "$FHS" -c "$SRC/out/views/views_shell --ozone-platform=wayland --bar --theme $THEME"
  step run-plain-gpu $?
  grep -E 'first attach|attaches|colours|alive|PSS|fd kinds|renderer' "$RES/w3c-plain-gpu/summary.txt" | sort -u
fi

# The PR's prove commands, as written in PROVE.md, with R = this worktree.
if has claims; then
  R="$WT"
  bash "$R/tools/bench/worker/headless.sh" ~/views-bench/results/w3c-switch ~/views-bench/build-env -c "bash $R/tools/bench/worker/session-demo.sh ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar --theme $R/examples/themes/all-black --demo-workspace-switch" > /dev/null && grep -q 'ECHO workspace 3' ~/views-bench/results/w3c-switch/client.log && grep -q '"name": "3"' ~/views-bench/results/w3c-switch/tree.json && ! grep -q '"app_id": "views-shell"' ~/views-bench/results/w3c-switch/tree.json && grep -qE 'colours: ([2-9]|[1-9][0-9]+)' ~/views-bench/results/w3c-switch/summary.txt
  step claim-C21.1 $?
  bash "$R/tools/bench/worker/headless.sh" ~/views-bench/results/w3c-notify ~/views-bench/build-env -c "bash $R/tools/bench/worker/session-demo.sh ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar --theme $R/examples/themes/all-black" > /dev/null && [ "$(grep -oE '^ *[0-9]+ zwlr_layer_shell_v1[#@][0-9]+\.get_layer_surface' ~/views-bench/results/w3c-notify/summary.txt | awk '{s+=$1} END{print s+0}')" -ge 2 ] && grep -q 'get_layer_surface(.*"views-shell-notification")' ~/views-bench/results/w3c-notify/client.log
  step claim-C21.2 $?
  bash "$R/tools/bench/worker/headless.sh" ~/views-bench/results/w3c-keyboard ~/views-bench/build-env -c "bash $R/tools/bench/worker/session-demo.sh ~/chromium/src/out/views/views_shell --ozone-platform=wayland --use-gl=angle --use-angle=swiftshader --bar --theme $R/examples/themes/all-black --demo-keyboard" > /dev/null && grep -q 'TYPED hello' ~/views-bench/results/w3c-keyboard/client.log
  step claim-C21.3 $?
  grep -E 'TYPED|keyboard-probe|wl_keyboard[#@][0-9]+\.(enter|leave)' ~/views-bench/results/w3c-keyboard/client.log | head -n 20
  ~/views-bench/build-env -c "cd ~/chromium/src && out/views/views_shell_unittests --gtest_filter=WorkspaceStrip*" > "$RES/w3c-claim-21.4.log" 2>&1
  rc=$?; grep -E 'tests? ran|PASSED|FAILED' "$RES/w3c-claim-21.4.log"; step claim-C21.4 $rc
fi

# The assembled program on the release build: the real footprint.
if has release; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh release $WT/tools/bench/args.release.gn && autoninja -C out/release views_shell"
  rc=$?; step release-build $rc
  if [ $rc = 0 ]; then
    CLIENT_FD_DUMP=1 bash "$WT/tools/bench/worker/headless.sh" "$RES/w3c-release" "$FHS" -c "$SRC/out/release/views_shell $GL --bar --theme $THEME"
    step run-release $?
    grep -E 'wm:|notifications:|FATAL|Check failed' "$RES/w3c-release/client.log" | head
    grep -E 'first attach|attaches|colours|alive|PSS|fd kinds' "$RES/w3c-release/summary.txt" | sort -u
  fi
fi

echo "w3c-seq: done, fail=$fail"
exit $fail
