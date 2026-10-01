#!/usr/bin/env bash
# w2c's whole bench sequence (chapter 2: the notifications library). Runs on
# the bench, outside the FHS environment (runtime-test must not nest inside
# it), under ~/views-bench/bench.lock, as a transient user unit: see
# tools/bench/README.md. Stages, all by default, or the ones named:
#   wire     pristine checkout, this worktree's shell/ as src/views_shell, patches
#   build    ensure-out.sh (out/views back to tools/bench/args.views.gn, gn gen,
#            which also checks the executable's assert_no_deps), gn check, build
#            views_shell (the seam still links) and views_shell_unittests
#   graph    gn desc of //views_shell/notifications:notifications deps (C16.3)
#   display  every Notification* and ShellMessagePopup* test, inside runtime-test
#            with a private session bus (dbus-run-session) and a private headless
#            scroll as the Wayland display the Views tests need (C16.1)
#   bus      the same filter under dbus-run-session only (started outside the
#            FHS environment), no display: the bus tests run, the Views tests skip
#   nobus    NotificationServer* with DBUS_SESSION_BUS_ADDRESS unset: the bus
#            tests skip (C16.2)
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
RT="$HOME/.local/bin/runtime-test"
FILTER='Notification*:ShellMessagePopup*'
STAGES="${*:-wire build graph display bus nobus}"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }
summarize() {  # summarize <log>: the launcher's verdict lines
  grep -E '^\[ *(OK|SKIPPED|FAILED|RUN) *\]|tests? (passed|failed|skipped)|SUCCESS|No matching tests|Check failed|FATAL' "$1" | tail -n 60
  echo "OK lines: $(grep -cE '^\[ +OK \]' "$1")  SKIPPED lines: $(grep -cE '^\[ +SKIPPED \]' "$1")  FAILED lines: $(grep -cE '^\[ +FAILED +\]' "$1")"
}
mkdir -p "$RES"

echo "w2c-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1); rc=$?
  echo "$out"
  last=$(printf '%s\n' "$out" | tail -n 1)
  case "$last" in WIRE-OK*) step wire 0;; *) step wire 1; echo "w2c-seq: wire failed, stopping"; exit 1;; esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step gn-gen $rc
  [ $rc = 0 ] || { echo "w2c-seq: gn gen failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -k 0 -C out/views views_shell views_shell_unittests"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w2c-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests"
fi

if has graph; then
  "$FHS" -c "cd $SRC && gn desc out/views //views_shell/notifications:notifications deps" > "$RES/w2c-graph-deps.txt" 2>&1
  step gn-desc $?
  echo "message_center: $(grep -c '^//ui/message_center:message_center$' "$RES/w2c-graph-deps.txt") dbus: $(grep -c '^//dbus:dbus$' "$RES/w2c-graph-deps.txt") components/dbus: $(grep -c '^//components/dbus' "$RES/w2c-graph-deps.txt")"
  "$FHS" -c "cd $SRC && gn desc out/views //views_shell/notifications:notifications deps --all" > "$RES/w2c-graph-deps-all.txt" 2>&1
  step gn-desc-all $?
  echo "transitive deps: $(grep -c '^//' "$RES/w2c-graph-deps-all.txt"), under //components/dbus: $(grep -c '^//components/dbus' "$RES/w2c-graph-deps-all.txt")"
fi

if has display; then
  # Inside the private runtime: a scratch HOME (the checkout and depot_tools
  # linked in, as headless.sh does), a private session bus, a headless scroll
  # on pixman, then the tests as its Wayland client. Nothing reaches the
  # bench user's live runtime.
  out="$RES/w2c-display"; rm -rf "$out"; mkdir -p "$out"
  "$RT" -- env OUT="$out" FHS="$FHS" FILTER="$FILTER" REAL_HOME="$HOME" \
    SCROLL="$B/scroll/bin/scroll" \
    HOME="$out/home" XDG_CONFIG_HOME="$out/home/.config" XDG_CACHE_HOME="$out/home/.cache" \
    XDG_STATE_HOME="$out/home/.local/state" XDG_DATA_HOME="$out/home/.local/share" \
    DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent \
    dbus-run-session -- bash -c '
set -u
if [ -e "$XDG_RUNTIME_DIR/systemd" ]; then echo "refusing: live runtime" >&2; exit 3; fi
mkdir -p "$XDG_CONFIG_HOME/scroll"
for d in chromium depot_tools; do [ -e "$REAL_HOME/$d" ] && ln -s "$REAL_HOME/$d" "$HOME/$d"; done
printf "output HEADLESS-1 resolution 1920x1080 scale 1\n" > "$XDG_CONFIG_HOME/scroll/config"
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1 \
  "$SCROLL" -c "$XDG_CONFIG_HOME/scroll/config" > "$OUT/scroll.log" 2>&1 & SP=$!
for i in $(seq 1 200); do ls "$XDG_RUNTIME_DIR"/wayland-* >/dev/null 2>&1 && break; sleep 0.05; done
export WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v lock | head -1)")
echo "display: $WAYLAND_DISPLAY bus: ${DBUS_SESSION_BUS_ADDRESS:-unset} scroll pid $SP"
"$FHS" -c "cd ~/chromium/src && out/views/views_shell_unittests --gtest_filter=$FILTER --test-launcher-jobs=1" > "$OUT/unittests.log" 2>&1
rc=$?
echo "unittests rc $rc"
kill $SP 2>/dev/null; wait $SP 2>/dev/null
exit $rc'
  rc=$?; step display $rc
  summarize "$out/unittests.log"
fi

if has bus; then
  # dbus-run-session outside the FHS environment: inside it, the FHS's
  # session.conf has no <listen> element and the daemon does not start.
  env -u WAYLAND_DISPLAY dbus-run-session -- "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='$FILTER' --test-launcher-jobs=1" > "$RES/w2c-bus.log" 2>&1
  rc=$?; step bus-only $rc
  summarize "$RES/w2c-bus.log"
fi

if has nobus; then
  "$FHS" -c "cd $SRC && env -u DBUS_SESSION_BUS_ADDRESS out/views/views_shell_unittests --gtest_filter='NotificationServer*' --test-launcher-jobs=1" > "$RES/w2c-nobus.log" 2>&1
  rc=$?; step nobus $rc
  summarize "$RES/w2c-nobus.log"
fi

echo "w2c-seq: done, fail=$fail"
exit $fail
