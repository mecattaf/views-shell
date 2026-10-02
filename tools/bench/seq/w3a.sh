#!/usr/bin/env bash
# w3a's whole bench sequence (chapter 2: the ui-tree renderer). Runs on the
# bench, outside the FHS environment (runtime-test must not nest inside it),
# under ~/views-bench/bench.lock, as a transient user unit: see
# tools/bench/README.md. Stages, all by default, or the ones named:
#   wire     pristine checkout, this worktree's shell/ as src/views_shell, the
#            repository data mirrored to src/views_shell/data, patches
#   build    ensure-out.sh (out/views back to tools/bench/args.views.gn, gn gen),
#            gn check of //views_shell/*, build views_shell (the seam links into
#            the program) and views_shell_unittests
#   format   clang-format --dry-run over shell/ui_tree (Chromium's style file)
#   plain    the C19.1 prove command as written: the ui-tree filter in the FHS
#            environment with no display (the views are built outside a Widget)
#   display  the same filter inside runtime-test against a private headless
#            scroll, so every rendered root is shown in a real Widget
#   graph    gn desc of //views_shell/ui_tree:ui_tree deps (what the seam pulls)
# Every step echoes its rc; the script exits non-zero if any step failed.
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"
FHS="$B/build-env"
SRC="$HOME/chromium/src"
RES="$B/results"
RT="$HOME/.local/bin/runtime-test"
FILTER='UiTree*:Binding*:RenderTrace*:ListItem*'
STAGES="${*:-wire build format plain display graph}"
fail=0
has() { case " $STAGES " in *" $1 "*) return 0;; esac; return 1; }
step() { echo "STEP $1 rc $2"; [ "$2" = 0 ] || fail=1; }
summarize() {  # summarize <log>: the launcher's verdict lines
  grep -E '^\[ *(OK|SKIPPED|FAILED) *\]|tests? (passed|failed|skipped)|SUCCESS|No matching tests|Check failed|FATAL' "$1" | tail -n 80
  echo "OK lines: $(grep -cE '^\[ +OK \]' "$1")  SKIPPED lines: $(grep -cE '^\[ +SKIPPED \]' "$1")  FAILED lines: $(grep -cE '^\[ +FAILED +\]' "$1")"
}
mkdir -p "$RES"

echo "w3a-seq: $(date -u +%FT%TZ) worktree $WT stages: $STAGES"

if has wire; then
  out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1)
  echo "$out"
  last=$(printf '%s\n' "$out" | tail -n 1)
  case "$last" in WIRE-OK*) step wire 0;; *) step wire 1; echo "w3a-seq: wire failed, stopping"; exit 1;; esac
fi

if has build; then
  "$FHS" -c "cd $SRC && bash $WT/tools/bench/worker/ensure-out.sh views $WT/tools/bench/args.views.gn"
  rc=$?; step gn-gen $rc
  [ $rc = 0 ] || { echo "w3a-seq: gn gen failed, stopping"; exit 1; }
  "$FHS" -c "cd $SRC && gn check out/views '//views_shell/*'"
  step gn-check $?
  "$FHS" -c "cd $SRC && autoninja -k 0 -C out/views views_shell views_shell_unittests"
  rc=$?; step build $rc
  [ $rc = 0 ] || { echo "w3a-seq: build failed, stopping"; exit 1; }
  ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_shell_unittests"
fi

if has format; then
  "$FHS" -c "cd $SRC && buildtools/linux64-format/clang-format --dry-run --Werror views_shell/ui_tree/*.h views_shell/ui_tree/*.cc" > "$RES/w3a-format.log" 2>&1
  rc=$?; step format $rc
  head -n 40 "$RES/w3a-format.log"
fi

if has plain; then
  # The prove command of C19.1, verbatim apart from the log.
  "$FHS" -c "cd $SRC && out/views/views_shell_unittests --gtest_filter='$FILTER'" > "$RES/w3a-plain.log" 2>&1
  rc=$?; step plain $rc
  summarize "$RES/w3a-plain.log"
fi

if has display; then
  # Inside the private runtime: a scratch HOME (the checkout and depot_tools
  # linked in, as headless.sh does), a headless scroll on pixman, then the
  # tests as its Wayland client. Nothing reaches the bench user's live runtime.
  out="$RES/w3a-display"; rm -rf "$out"; mkdir -p "$out"
  "$RT" -- env OUT="$out" FHS="$FHS" FILTER="$FILTER" REAL_HOME="$HOME" \
    SCROLL="$B/scroll/bin/scroll" \
    HOME="$out/home" XDG_CONFIG_HOME="$out/home/.config" XDG_CACHE_HOME="$out/home/.cache" \
    XDG_STATE_HOME="$out/home/.local/state" XDG_DATA_HOME="$out/home/.local/share" \
    DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent \
    bash -c '
set -u
if [ -e "$XDG_RUNTIME_DIR/systemd" ]; then echo "refusing: live runtime" >&2; exit 3; fi
mkdir -p "$XDG_CONFIG_HOME/scroll"
for d in chromium depot_tools; do [ -e "$REAL_HOME/$d" ] && ln -s "$REAL_HOME/$d" "$HOME/$d"; done
printf "output HEADLESS-1 resolution 1920x1080 scale 1\n" > "$XDG_CONFIG_HOME/scroll/config"
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1 \
  "$SCROLL" -c "$XDG_CONFIG_HOME/scroll/config" > "$OUT/scroll.log" 2>&1 & SP=$!
for i in $(seq 1 200); do ls "$XDG_RUNTIME_DIR"/wayland-* >/dev/null 2>&1 && break; sleep 0.05; done
export WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v lock | head -1)")
echo "display: $WAYLAND_DISPLAY scroll pid $SP"
"$FHS" -c "cd ~/chromium/src && out/views/views_shell_unittests --gtest_filter=$FILTER --test-launcher-jobs=1" > "$OUT/unittests.log" 2>&1
rc=$?
echo "unittests rc $rc"
kill $SP 2>/dev/null; wait $SP 2>/dev/null
exit $rc'
  rc=$?; step display $rc
  summarize "$out/unittests.log"
  echo "built without a Widget (display run): $(grep -c 'built without a Widget' "$out/unittests.log")"
fi

if has graph; then
  "$FHS" -c "cd $SRC && gn desc out/views //views_shell/ui_tree:ui_tree deps" > "$RES/w3a-graph-deps.txt" 2>&1
  step gn-desc $?
  grep '^//' "$RES/w3a-graph-deps.txt"
fi

echo "w3a-seq: done, fail=$fail"
exit $fail
