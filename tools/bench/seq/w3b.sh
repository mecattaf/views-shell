#!/usr/bin/env bash
# w3b's whole bench sequence (chapter 2: the plugin host in shell/plugins).
# Runs on the bench, outside the FHS environment, under
# ~/views-bench/bench.lock, as a transient user unit: see tools/bench/README.md.
# Stages, all by default, or the ones named:
#   wire     pristine checkout, this worktree's shell/ as src/views_shell (and
#            examples/, schemas/, tools/fixtures/ under views_shell/data), patches
#   build    ensure-out.sh (args restore, gn gen), gn check of
#            //views_shell/plugins, then views_shell, views_shell_unittests and
#            views_shell/plugins:plugin_probe
#   tests    views_shell_unittests --gtest_filter=Plugin*:ProcessPlugin*:
#            Declarative*:Permissions*:JsonRpc* (the C20.1 filter)
#   probes   plugin_probe against echo-process (ping, try-exec) and against
#            music-scratchpad (a T1 plugin, with and without its capability)
#   isolated the ping probe inside ~/.local/bin/runtime-test (private /run/user,
#            so no user manager): the plain launch path, logged as such
#   claims   the C20.1-C20.3 commands verbatim, each log kept in
#            ~/views-bench/results/w3b-c20.<n>.log: out/views is relinked by
#            whichever item holds the lock next, so the PROVE rows read these
#            logs rather than the live binaries
# Every step echoes its rc; the script exits non-zero if any step failed.
# Nothing here runs a compositor: the plugin host is a console library, and
# plugins are plain child processes (no user manager inside the FHS env, so
# ProcessPlugin logs which launch path it took).
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
STAGES="${*:-wire build tests probes isolated claims}"
FILTER='Plugin*:ProcessPlugin*:Declarative*:Permissions*:JsonRpc*'
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }
quiet() { grep -vE '^\[views-shell-build-env\]|^  (depot_tools|ccache):'; }

echo "w3b-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"
mkdir -p "$RES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1)
  echo "$out"
  case "$(printf '%s\n' "$out" | tail -n 1)" in
    WIRE-OK*) step wire 0;;
    *) step wire 1; echo "w3b-seq: wire failed, stopping"; exit 1;;
  esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step ensure-out $rc
  [ $rc = 0 ] || { echo "w3b-seq: ensure-out failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/plugins/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -k 0 -C out/views views_shell views_shell_unittests views_shell/plugins:plugin_probe"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w3b-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests" "$SRC/out/views/plugin_probe"
fi

if has tests; then
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='$FILTER'" > "$RES/w3b-unittests.log" 2>&1
  rc=$?; step unittests $rc
  grep -E '^\[ *(OK|FAILED|PASSED|RUN|SKIPPED) *\]|tests? (ran|passed)|SUCCESS|FAILURE|launch path taken|launched pid|no systemd user scope' "$RES/w3b-unittests.log" | tail -n 80
  echo "OK lines: $(grep -c '^\[       OK \]' "$RES/w3b-unittests.log")"
fi

probe() {  # probe <name> <expected rc> <probe args...>
  local name="$1" want="$2"; shift 2
  "$FHS" -c "cd $SRC && out/views/plugin_probe $*" > "$RES/w3b-probe-$name.log" 2> "$RES/w3b-probe-$name.err"
  local rc=$?
  quiet < "$RES/w3b-probe-$name.log" | grep -vE '^  |^\{|^\}' | head -n 40
  grep -E 'launched pid|no systemd user scope|REJECT|refused' "$RES/w3b-probe-$name.err" | head -n 5
  [ "$rc" = "$want" ]; step "probe-$name (rc $rc, want $want)" $?
}

if has probes; then
  probe ping 0 "--plugin views_shell/data/examples/echo-process --invoke ping --args '{\"text\":\"hi\"}'"
  probe try-exec 0 "--plugin views_shell/data/examples/echo-process --invoke try-exec --seconds 3"
  probe t1-gated 1 "--plugin views_shell/data/examples/music-scratchpad --seconds 1"
  probe t1 0 "--plugin views_shell/data/examples/music-scratchpad --capabilities scratchpad.toggle --seconds 1"
fi

if has isolated; then
  ~/.local/bin/runtime-test -- "$FHS" -c "cd $SRC && out/views/plugin_probe --plugin views_shell/data/examples/echo-process --invoke ping --args '{\"text\":\"hi\"}'" > "$RES/w3b-isolated.log" 2> "$RES/w3b-isolated.err"
  rc=$?
  grep -E '^(invoke|plugin alive|launch path|shutdown)' "$RES/w3b-isolated.log"
  grep -E 'launched pid|no systemd user scope' "$RES/w3b-isolated.err" | sed -E 's#/home/[^ ]*#~/...#g'
  [ "$rc" = 0 ] && grep -q '^launch path: plain' "$RES/w3b-isolated.log"; step "isolated (rc $rc, plain path)" $?
fi

if has claims; then
  # C20.1, verbatim.
  "$FHS" -c "cd ~/chromium/src && out/views/views_shell_unittests --gtest_filter=Plugin*:ProcessPlugin*:Declarative*:Permissions*:JsonRpc*" > "$RES/w3b-c20.1.log" 2>&1
  rc=$?; step "claim C20.1 (OK $(grep -c '^\[       OK \]' "$RES/w3b-c20.1.log"))" $rc
  # C20.2, verbatim.
  "$FHS" -c "cd ~/chromium/src && out/views/plugin_probe --plugin views_shell/data/examples/echo-process --invoke ping --args {\\\"text\\\":\\\"hi\\\"}" > "$RES/w3b-c20.2.log" 2>&1
  rc=$?; step "claim C20.2" $rc
  grep -E '^(surface/setTree|snapshot|invoke|notify|plugin alive|launch path|shutdown)' "$RES/w3b-c20.2.log" | cut -c1-200
  # C20.3, verbatim.
  "$FHS" -c "cd ~/chromium/src && out/views/plugin_probe --plugin views_shell/data/examples/echo-process --invoke try-exec" > "$RES/w3b-c20.3.log" 2>&1
  rc=$?; step "claim C20.3" $rc
  grep -E '^(invoke|plugin alive|shutdown)' "$RES/w3b-c20.3.log" | cut -c1-240
  ( cd "$SRC" && ls -l --time-style=+%FT%T out/views/views_shell_unittests out/views/plugin_probe ) > "$RES/w3b-claims-binaries.txt"
  cat "$RES/w3b-claims-binaries.txt"
fi

echo "w3b-seq: done, fail=$fail"
exit $fail
