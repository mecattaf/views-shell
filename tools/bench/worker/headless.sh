#!/usr/bin/env bash
# Run <cmd...> as a client of a headless stock scroll, under runtime-test, and collect evidence.
#   headless.sh <outdir> <cmd...>
# Writes to <outdir>: scroll.log, client.log (WAYLAND_DEBUG=1), tree.json (scrollmsg -t get_tree),
# outputs.json, shot.png (grim, after CLIENT_SETTLE seconds, default 4), measure.tsv (one row, PSS etc.),
# and summary.txt. Exit 0 when the client was still alive at capture time and the screenshot exists.
# Never run outside runtime-test: the outer wrapper here refuses a live runtime dir (rule R19).
set -uo pipefail
OUT="${1:?usage: headless.sh <outdir> <cmd...>}"; shift
[ "$#" -gt 0 ] || { echo 'headless.sh: missing client command' >&2; exit 2; }
B="$HOME/views-bench"; SCROLL="$B/scroll/bin/scroll"; SCROLLMSG="$B/scroll/bin/scrollmsg"
RT="$HOME/.local/bin/runtime-test"
[ -x "$RT" ] || { echo 'headless.sh: runtime-test missing on the bench' >&2; exit 1; }
[ -x "$SCROLL" ] || { echo 'headless.sh: no scroll at $SCROLL' >&2; exit 1; }
mkdir -p "$OUT"
HERE="$(cd "$(dirname "$0")" && pwd)"
# Everything below runs inside the private runtime. HOME is scratch so nothing reaches real config.
exec "$RT" -- env OUT="$OUT" SCROLL="$SCROLL" SCROLLMSG="$SCROLLMSG" HERE="$HERE" CLIENT_SETTLE="${CLIENT_SETTLE:-4}" \
  HOME="$OUT/home" XDG_CONFIG_HOME="$OUT/home/.config" XDG_CACHE_HOME="$OUT/home/.cache" XDG_STATE_HOME="$OUT/home/.local/state" XDG_DATA_HOME="$OUT/home/.local/share" \
  DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent \
  dbus-run-session -- bash -c '
set -u
if [ -e "$XDG_RUNTIME_DIR/systemd" ]; then echo "refusing: live runtime" >&2; exit 3; fi
rm -rf "$HOME"; mkdir -p "$XDG_CONFIG_HOME/scroll"
printf "output HEADLESS-1 resolution 1920x1080 scale 1\n" > "$XDG_CONFIG_HOME/scroll/config"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1
"$SCROLL" -c "$XDG_CONFIG_HOME/scroll/config" > "$OUT/scroll.log" 2>&1 & SP=$!
for i in $(seq 1 200); do ls "$XDG_RUNTIME_DIR"/wayland-* >/dev/null 2>&1 && break; sleep 0.05; done
export WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v lock | head -1)")
export SCROLLSOCK=$(ls "$XDG_RUNTIME_DIR"/*-ipc.* 2>/dev/null | head -1); export SWAYSOCK="$SCROLLSOCK"
echo "compositor: $WAYLAND_DISPLAY $SCROLLSOCK scroll pid $SP" | tee "$OUT/summary.txt"
"$SCROLLMSG" -t get_outputs -r > "$OUT/outputs.json" 2>/dev/null
echo "client: $*" | tee -a "$OUT/summary.txt"
t0=$(date +%s%N)
WAYLAND_DEBUG=1 "$@" > "$OUT/client.log" 2>&1 & CP=$!
for i in $(seq 1 3000); do grep -qE "wl_surface[#@][0-9]+\.attach" "$OUT/client.log" && break; sleep 0.005; done
t1=$(date +%s%N); echo "first attach after $(( (t1-t0)/1000000 )) ms" | tee -a "$OUT/summary.txt"
sleep "$CLIENT_SETTLE"
alive=0; kill -0 $CP 2>/dev/null && alive=1
echo "client alive at capture: $alive" | tee -a "$OUT/summary.txt"
"$SCROLLMSG" -t get_tree -r > "$OUT/tree.json" 2>/dev/null
grim "$OUT/shot.png" 2>>"$OUT/summary.txt" && echo "screenshot: $(stat -c %s "$OUT/shot.png") bytes" | tee -a "$OUT/summary.txt"
grep -oE "zwlr_layer_shell_v1[#@][0-9]+\.get_layer_surface|zwlr_layer_surface_v1[#@][0-9]+\.configure|xdg_wm_base[#@][0-9]+\.get_xdg_surface|xdg_surface[#@][0-9]+\.get_popup|xdg_surface[#@][0-9]+\.get_toplevel" "$OUT/client.log" | sort | uniq -c | tee -a "$OUT/summary.txt"
bash "$HERE/measure.sh" client 10 $CP "$OUT/measure.tsv" | tee -a "$OUT/summary.txt"
kill $CP 2>/dev/null; wait $CP 2>/dev/null
kill $SP 2>/dev/null; wait $SP 2>/dev/null
[ "$alive" = 1 ] && [ -s "$OUT/shot.png" ]
' _ "$@"
