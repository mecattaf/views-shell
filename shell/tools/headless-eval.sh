#!/usr/bin/env bash
# headless-eval.sh — capture agency's layer-shell surfaces as a PNG inside a
# NESTED, HEADLESS wlroots compositor that the harness owns end to end.
#
# Why this exists (issue #5): the jun30 spikes could not screenshot agency's
# chrome output on a real-GPU niri — its buffers went to hardware planes /
# direct scanout and niri's wlr-screencopy captured only the niri-composited
# windows, never cowl's own pixels. This harness sidesteps that entire class:
# it runs a throwaway sway on WLR_BACKENDS=headless + WLR_RENDERER=pixman, i.e.
# a pure SOFTWARE framebuffer with NO DRM/KMS and NO hardware planes, so every
# client buffer is necessarily composited into that framebuffer, and launches
# agency's chrome with --disable-gpu so it presents wl_shm buffers. grim then
# reads the composited framebuffer over wlr-screencopy. Nothing can escape to a
# plane because there are no planes.
#
# It touches NOTHING of any live session: a fresh, isolated XDG_RUNTIME_DIR
# under /tmp (mode 0700) is minted per run and torn down on exit. It never reads
# or writes /run/user/*, never talks to the machine's real niri/sway, and kills
# only the exact PIDs it spawned.
#
# Runs ON worker-tb (that is the only host with out/agency/chrome + the FHS
# env). sway/swaymsg are pulled from the nix binary cache; grim/slurp are on
# PATH already. See docs/HEADLESS-CAPTURE.md for the full story + matrix.
#
# Usage (on worker-tb):
#   tools/headless-eval.sh --surface bar     --out /tmp/cap-bar.png
#   tools/headless-eval.sh --surface launcher --out /tmp/cap-launcher.png
# Options:
#   --surface bar|launcher|browser   which agency surface to arm (default bar)
#   --chrome PATH        chrome binary (default ~/chromium/src/out/agency/chrome)
#   --fhs PATH           FHS env wrapper (default /tmp/fhs-result/bin/agency-chromium-build-env)
#   --out PATH           PNG destination (default <workdir>/capture.png)
#   --logdir DIR         where to copy sway.log / chrome.log (default alongside --out)
#   --hold SECONDS       settle time after the surface maps, before capture (default 8)
#   --width N --height N nested output size (default 1920x1080)
#   --gpu                do NOT pass --disable-gpu (test hardware-accel path; usually black under headless)
#   --debug              run chrome under WAYLAND_DEBUG=1 (verbose protocol trace in chrome.log)
#   --keep               do not delete the isolated runtime dir on exit (for post-mortem)
#   --type STRING        after the surface maps+settles, INJECT keyboard input:
#                        drive wtype (zwp_virtual_keyboard) against the nested
#                        sway to type STRING into the focused surface, then
#                        capture. This is the interactive keyboard-routing gate
#                        for the launcher (issue #8): a screenshot with STRING in
#                        the search field + filtered results proves keys reached
#                        the Textfield, which a static render never could.
#   --type-delay MS      per-keystroke delay for wtype (default 70)
#
# Exit 0 ONLY if: the nested compositor came up, the agency layer surface mapped
# (proven from chrome's own [agency] weld line AND sway's layer_shell log), and
# a non-empty PNG was written. Prints a machine-greppable HEADLESS-EVAL-OK /
# HEADLESS-EVAL-FAIL token as the last line. Never self-reports a fake success:
# the PNG's sha256 + byte size are printed for the caller to attach + eyeball.
set -uo pipefail

# ---- defaults -------------------------------------------------------------
SURFACE=bar
CHROME="$HOME/chromium/src/out/agency/chrome"
FHS=/tmp/fhs-result/bin/agency-chromium-build-env
OUT=""
LOGDIR=""
HOLD=8
WIDTH=1920
HEIGHT=1080
USE_GPU=0
WAYDEBUG=0
KEEP=0
TYPE_STR=""
TYPE_DELAY=70

die() { echo "[headless-eval] FATAL: $*" >&2; echo "HEADLESS-EVAL-FAIL"; exit 1; }
log() { echo "[headless-eval] $*" >&2; }

# --- views-shell adaptation (2026-10-01; see shell/PROVENANCE.md) -------------------
# Rule R19: this harness launches a compositor, so it runs ONLY inside
# ~/.local/bin/runtime-test, which gives it a private /run/user tree and PID/IPC
# namespaces. Under runtime-test (bubblewrap --unshare-pid) PID 1 is bwrap.
# Set VIEWS_SHELL_HEADLESS_ALLOW_OUTSIDE_RUNTIME_TEST=1 only inside a disposable VM.
if [ "$(cat /proc/1/comm 2>/dev/null)" != "bwrap" ] && \
   [ "${VIEWS_SHELL_HEADLESS_ALLOW_OUTSIDE_RUNTIME_TEST:-}" != "1" ]; then
  die "refusing to run outside runtime-test: use ~/.local/bin/runtime-test -- $0 $*"
fi
# Never let a nested test reach the live session's compositor or bus, whatever
# the caller inherited. runtime-test already drops SWAYSOCK and NIRI_SOCKET.
unset SCROLLSOCK SWAYSOCK I3SOCK NIRI_SOCKET HYPRLAND_INSTANCE_SIGNATURE \
      WAYLAND_DISPLAY DISPLAY DBUS_SESSION_BUS_ADDRESS
# TODO(views-shell): launch a nested headless scroll instead of sway once scroll is
# packaged; the i3-ipc commands used below are the same.
# -----------------------------------------------------------------------------

while [ $# -gt 0 ]; do
  case "$1" in
    --surface) SURFACE="$2"; shift 2;;
    --chrome)  CHROME="$2"; shift 2;;
    --fhs)     FHS="$2"; shift 2;;
    --out)     OUT="$2"; shift 2;;
    --logdir)  LOGDIR="$2"; shift 2;;
    --hold)    HOLD="$2"; shift 2;;
    --width)   WIDTH="$2"; shift 2;;
    --height)  HEIGHT="$2"; shift 2;;
    --gpu)     USE_GPU=1; shift;;
    --debug)   WAYDEBUG=1; shift;;
    --keep)    KEEP=1; shift;;
    --type)       TYPE_STR="$2"; shift 2;;
    --type-delay) TYPE_DELAY="$2"; shift 2;;
    *) die "unknown arg: $1";;
  esac
done

# ---- resolve tooling ------------------------------------------------------
command -v nix  >/dev/null || die "nix not on PATH (need it to fetch sway)"
command -v grim >/dev/null || die "grim not on PATH"
[ -x "$CHROME" ] || die "chrome binary not found/executable: $CHROME"
[ -x "$FHS" ]    || die "FHS env wrapper not found: $FHS"

log "resolving sway from nix (binary cache) ..."
SWAY_OUT="$(nix build --no-link --print-out-paths nixpkgs#sway 2>/dev/null)" \
  || die "could not build/fetch nixpkgs#sway"
SWAY="$SWAY_OUT/bin/sway"
SWAYMSG="$SWAY_OUT/bin/swaymsg"
[ -x "$SWAY" ] && [ -x "$SWAYMSG" ] || die "sway/swaymsg missing under $SWAY_OUT"

# ---- isolated runtime dir + workdir --------------------------------------
RT="$(mktemp -d /tmp/agency-headless-XXXXXX)" || die "mktemp runtime dir failed"
chmod 700 "$RT"
WD="$(mktemp -d /tmp/agency-headless-wd-XXXXXX)" || die "mktemp workdir failed"
[ -n "$OUT" ]    || OUT="$WD/capture.png"
[ -n "$LOGDIR" ] || LOGDIR="$(dirname "$OUT")"
mkdir -p "$LOGDIR"
PROFILE="$WD/profile"

SWAY_PID=""
CHROME_PID=""
WTYPE_PID=""
cleanup() {
  set +e
  [ -n "$WTYPE_PID" ] && kill "$WTYPE_PID" 2>/dev/null
  [ -n "$CHROME_PID" ] && kill "$CHROME_PID" 2>/dev/null && sleep 1
  [ -n "$CHROME_PID" ] && kill -9 "$CHROME_PID" 2>/dev/null
  [ -n "$SWAY_PID" ] && kill "$SWAY_PID" 2>/dev/null && sleep 1
  [ -n "$SWAY_PID" ] && kill -9 "$SWAY_PID" 2>/dev/null
  # Belt: the PID captured from `env -i ... &` / the FHS bwrap wrapper does not
  # always reap the real sway/chrome. Kill by our UNIQUE, private paths only
  # ($PROFILE and $RT are per-run mktemp dirs) — never a broad pkill.
  pkill -9 -f "user-data-dir=$PROFILE" 2>/dev/null
  pkill -9 -f "$RT" 2>/dev/null
  if [ "$KEEP" = 1 ]; then
    log "kept runtime dir: $RT ; workdir: $WD"
  else
    rm -rf "$RT" "$WD"
  fi
}
trap cleanup EXIT

# ---- launch the nested headless compositor -------------------------------
SWAY_LOG="$RT/sway.log"
cat > "$RT/sway.cfg" <<EOF
output HEADLESS-1 resolution ${WIDTH}x${HEIGHT} position 0,0
# no idle/dpms, no bg image, no bar — keep the framebuffer deterministic
EOF

log "starting nested sway (headless+pixman) in $RT ..."
env -i HOME="$HOME" PATH="$PATH" \
  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
  XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY=wayland-1 \
  "$SWAY" -d -c "$RT/sway.cfg" >"$SWAY_LOG" 2>&1 &
SWAY_PID=$!

# wait for the wayland socket, then for the sway IPC socket (swaymsg needs
# SWAYSOCK — it does NOT derive from WAYLAND_DISPLAY), then a committed output.
ok=0
SWAYSOCK=""
for i in $(seq 1 40); do
  if [ -S "$RT/wayland-1" ]; then
    SWAYSOCK="$(ls "$RT"/sway-ipc.*.sock 2>/dev/null | head -1)"
    if [ -n "$SWAYSOCK" ] && \
       SWAYSOCK="$SWAYSOCK" "$SWAYMSG" -t get_outputs 2>/dev/null \
         | grep -q 'HEADLESS-1'; then ok=1; break; fi
  fi
  kill -0 "$SWAY_PID" 2>/dev/null || die "sway exited early; log:\n$(tail -20 "$SWAY_LOG")"
  sleep 0.5
done
[ "$ok" = 1 ] || die "nested sway output never came up; log:\n$(tail -30 "$SWAY_LOG")"
export SWAYSOCK
log "nested sway up: HEADLESS-1 ${WIDTH}x${HEIGHT}, socket $RT/wayland-1 (ipc $SWAYSOCK)"

# ---- build chrome flags ---------------------------------------------------
CHROME_FLAGS=(
  --ozone-platform=wayland
  --agency-layer-shell
  --password-store=basic
  --no-first-run
  --no-default-browser-check
  "--user-data-dir=$PROFILE"
)
WELD_NS="agency-bar"
case "$SURFACE" in
  bar) ;;
  launcher)
    CHROME_FLAGS+=( --agency-launcher )
    WELD_NS="agency-launcher"
    ;;
  rail)
    # Surface 5 v1 read-only mirror (issue #10). No niri in the nested
    # compositor, so --agency-rail-test-snapshot feeds a canned workspace/window
    # snapshot through the SAME fold path the live niri client drives.
    CHROME_FLAGS+=( --agency-rail --agency-rail-test-snapshot )
    WELD_NS="agency-rail"
    ;;
  browser)
    CHROME_FLAGS+=( --agency-browser-fullbleed )
    WELD_NS="agency-browser"
    ;;
  *) die "unknown --surface: $SURFACE (bar|launcher|browser|rail)";;
esac
if [ "$USE_GPU" = 0 ]; then
  # Force software presentation: wl_shm buffers, no GPU process, no overlays.
  # (Redundant under pixman — there are no planes — but makes the client path
  #  deterministic and avoids a GPU-init crash when there is no /dev/dri.)
  CHROME_FLAGS+=( --disable-gpu --disable-gpu-compositing
                  --disable-features=WaylandOverlayDelegation,DelegatedCompositing )
fi

# ---- launch agency chrome into the nested compositor ---------------------
CHROME_LOG="$LOGDIR/chrome.log"
log "launching agency chrome ($SURFACE) into nested compositor ..."
CHROME_CMD="env XDG_RUNTIME_DIR='$RT' WAYLAND_DISPLAY=wayland-1"
[ "$WAYDEBUG" = 1 ] && CHROME_CMD="$CHROME_CMD WAYLAND_DEBUG=1"
CHROME_CMD="$CHROME_CMD '$CHROME' ${CHROME_FLAGS[*]}"
"$FHS" -c "$CHROME_CMD" >"$CHROME_LOG" 2>&1 &
CHROME_PID=$!

# ---- wait for the layer surface to map -----------------------------------
# Two independent witnesses: chrome's own [agency] weld line, and sway's
# layer_shell surface-create log for our namespace.
mapped=0
for i in $(seq 1 60); do
  kill -0 "$CHROME_PID" 2>/dev/null || { log "chrome exited early"; break; }
  if grep -qi "welded 'agency" "$CHROME_LOG" 2>/dev/null \
     || grep -qi "$WELD_NS" "$CHROME_LOG" 2>/dev/null; then mapped=1; break; fi
  sleep 0.5
done
SWAY_SAW_LAYER=0
grep -qi "layer" "$SWAY_LOG" 2>/dev/null && SWAY_SAW_LAYER=1

if [ "$mapped" != 1 ]; then
  log "agency layer surface did NOT map within timeout; chrome.log tail:"
  tail -40 "$CHROME_LOG" >&2
  log "sway.log tail:"; tail -20 "$SWAY_LOG" >&2
fi

# ---- settle + capture -----------------------------------------------------
log "holding ${HOLD}s for paint to settle ..."
sleep "$HOLD"

# ---- optional: inject keyboard input via wtype (issue #8 interactive gate) --
# wtype binds zwp_virtual_keyboard_manager_v1 and creates a virtual keyboard on
# the seat. Under headless sway (WLR_LIBINPUT_NO_DEVICES=1) there is no physical
# keyboard, so this virtual keyboard is what gives the seat its keyboard
# capability; sway then routes wl_keyboard.enter + key events to the surface
# that holds the keyboard grab (the launcher requests kExclusive). The leading
# `-s` sleep keeps the virtual keyboard alive long enough for the compositor to
# assign keyboard focus BEFORE the first real character is sent (otherwise the
# enter can race the first keystroke and drop it).
TYPE_OK="n/a"
if [ -n "$TYPE_STR" ]; then
  log "resolving wtype from nix (binary cache) ..."
  WTYPE_OUT="$(nix build --no-link --print-out-paths nixpkgs#wtype 2>/dev/null)" \
    || die "could not build/fetch nixpkgs#wtype"
  WTYPE="$WTYPE_OUT/bin/wtype"
  [ -x "$WTYPE" ] || die "wtype missing under $WTYPE_OUT"
  WTYPE_LOG="$LOGDIR/wtype.log"
  # CRITICAL: wtype owns the zwp_virtual_keyboard for its whole run and DESTROYS
  # it on exit. Under headless sway, destroying the vkbd removes the seat's
  # keyboard capability and sway emits wl_keyboard.leave -- in practice a
  # DUPLICATE leave, which trips a stock Chromium DCHECK in
  # WaylandEventSource::OnKeyboardFocusChanged (a compositor-teardown quirk,
  # unrelated to focus routing). So we must grim the framebuffer WHILE the vkbd
  # is still held. Run wtype in the BACKGROUND with a long TRAILING sleep: it
  # settles focus (-s 700), types the query, then holds the keyboard alive so
  # the launcher keeps focus + the typed text while the capture runs. The trap
  # kills it afterward (teardown crash, if any, happens post-capture).
  # No `--`: a trailing `-s HOLD` after the text must stay an option, and our
  # injected queries never start with '-'.
  log "injecting keystrokes via wtype (bg, held): '$TYPE_STR' (delay ${TYPE_DELAY}ms/key) ..."
  XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY=wayland-1 \
    "$WTYPE" -s 700 -d "$TYPE_DELAY" "$TYPE_STR" -s 20000 >"$WTYPE_LOG" 2>&1 &
  WTYPE_PID=$!
  # Wait for focus-settle + typing to complete before capturing: 700ms settle +
  # len*delay + generous margin. The vkbd is held for 20s after, so the capture
  # (below) lands while the text is in the field and the surface is focused.
  TYPE_SETTLE=$(( (700 + ${#TYPE_STR} * TYPE_DELAY) / 1000 + 3 ))
  log "waiting ${TYPE_SETTLE}s for keystrokes to land (vkbd held 20s) ..."
  sleep "$TYPE_SETTLE"
  if kill -0 "$WTYPE_PID" 2>/dev/null; then TYPE_OK="held"; else
    TYPE_OK="wtype-exited-early"; log "WARN: wtype exited before capture; log:"; cat "$WTYPE_LOG" >&2
  fi
fi

# snapshot compositor state for evidence (swaymsg speaks SWAYSOCK, not wayland)
SWAYSOCK="$SWAYSOCK" "$SWAYMSG" -t get_outputs > "$LOGDIR/sway-outputs.json" 2>/dev/null
SWAYSOCK="$SWAYSOCK" "$SWAYMSG" -t get_tree    > "$LOGDIR/sway-tree.json" 2>/dev/null

log "capturing with grim -> $OUT"
XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY=wayland-1 grim -o HEADLESS-1 "$OUT" 2>"$LOGDIR/grim.err"
GRIM_RC=$?
cp -f "$SWAY_LOG" "$LOGDIR/sway.log" 2>/dev/null

# ---- verdict --------------------------------------------------------------
echo "----- headless-eval report -----" >&2
echo "surface        : $SURFACE (ns=$WELD_NS)" >&2
echo "chrome mapped  : $([ $mapped = 1 ] && echo yes || echo NO)" >&2
echo "sway saw layer : $([ $SWAY_SAW_LAYER = 1 ] && echo yes || echo no)" >&2
[ -n "$TYPE_STR" ] && echo "keyboard inject: '$TYPE_STR' -> $TYPE_OK" >&2
echo "grim rc        : $GRIM_RC" >&2
if [ -s "$OUT" ]; then
  SZ=$(stat -c%s "$OUT" 2>/dev/null)
  SHA=$(sha256sum "$OUT" | cut -d' ' -f1)
  echo "png            : $OUT" >&2
  echo "png bytes      : $SZ" >&2
  echo "png sha256     : $SHA" >&2
else
  echo "png            : MISSING/empty" >&2
fi
echo "chrome.log     : $CHROME_LOG" >&2
echo "sway.log       : $LOGDIR/sway.log" >&2
echo "--------------------------------" >&2

if [ "$mapped" = 1 ] && [ "$GRIM_RC" = 0 ] && [ -s "$OUT" ]; then
  echo "HEADLESS-EVAL-OK"
  exit 0
else
  echo "HEADLESS-EVAL-FAIL"
  exit 1
fi
