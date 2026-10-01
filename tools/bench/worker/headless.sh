#!/usr/bin/env bash
# Run <cmd...> as a client of a headless stock scroll, under runtime-test, and collect evidence.
#   headless.sh <outdir> <cmd...>
# Writes to <outdir>: scroll.log, client.log (WAYLAND_DEBUG=1), tree.json (scrollmsg -t get_tree),
# outputs.json, shot.png (grim, after CLIENT_SETTLE seconds, default 4), measure.tsv (one row, PSS etc.),
# and summary.txt. Exit 0 when the client was still alive at capture time and the screenshot exists.
# Never run outside runtime-test: the outer wrapper here refuses a live runtime dir (rule R19).
# Environment (all optional):
#   CLIENT_SETTLE=<s>            seconds between the first attach and the capture (default 4)
#   CLIENT_FD_DUMP=1             after the settle, readlink every /proc/<pid>/fd entry of the client's
#                                process tree into fds-<pid>.txt and add "fd kinds:" lines to summary.txt
#   CLIENT_FD_RECHECK=<s>        with CLIENT_FD_DUMP, a second census <s> seconds after the first
#                                (fds-recheck-<pid>.txt, "fd kinds (recheck ...)") to show growth
#   BENCH_RUNTIME_TEST=<path>    the isolation wrapper (default ~/.local/bin/runtime-test); the bench's
#                                GPU variant is ~/views-bench/bin/runtime-test-gpu
#   BENCH_RUNTIME_TEST_ARGS=...  wrapper flags before "--", word-split (e.g. --allow-dri)
#   BENCH_WLR_RENDERER=<name>    the compositor's WLR_RENDERER (default pixman; gles2 needs a render node)
#   WLR_RENDER_DRM_DEVICE=<node> passed to the compositor when set (e.g. /dev/dri/renderD128)
set -uo pipefail
OUT="${1:?usage: headless.sh <outdir> <cmd...>}"; shift
[ "$#" -gt 0 ] || { echo 'headless.sh: missing client command' >&2; exit 2; }
B="$HOME/views-bench"; SCROLL="$B/scroll/bin/scroll"; SCROLLMSG="$B/scroll/bin/scrollmsg"
RT="${BENCH_RUNTIME_TEST:-$HOME/.local/bin/runtime-test}"; RT="${RT/#\~\//$HOME/}"
read -r -a RT_ARGS <<<"${BENCH_RUNTIME_TEST_ARGS:-}"
REAL_HOME="$HOME"   # the bench user's real home; the scratch HOME below hides it
[ -x "$RT" ] || { echo 'headless.sh: runtime-test missing on the bench' >&2; exit 1; }
[ -x "$SCROLL" ] || { echo 'headless.sh: no scroll at $SCROLL' >&2; exit 1; }
mkdir -p "$OUT"
HERE="$(cd "$(dirname "$0")" && pwd)"
# HOME becomes scratch below, so expand a "~/" in the client command against the real home now.
args=(); for a in "$@"; do a="${a/#\~\//$HOME/}"; args+=("${a// \~\// $HOME/}"); done; set -- "${args[@]}"
# Everything below runs inside the private runtime. HOME is scratch so nothing reaches real config.
exec "$RT" "${RT_ARGS[@]}" -- env OUT="$OUT" SCROLL="$SCROLL" SCROLLMSG="$SCROLLMSG" HERE="$HERE" CLIENT_SETTLE="${CLIENT_SETTLE:-4}" REAL_HOME="$REAL_HOME" \
  CLIENT_FD_DUMP="${CLIENT_FD_DUMP:-0}" CLIENT_FD_RECHECK="${CLIENT_FD_RECHECK:-0}" \
  BENCH_WLR_RENDERER="${BENCH_WLR_RENDERER:-pixman}" BENCH_DRM_DEVICE="${WLR_RENDER_DRM_DEVICE:-}" \
  HOME="$OUT/home" XDG_CONFIG_HOME="$OUT/home/.config" XDG_CACHE_HOME="$OUT/home/.cache" XDG_STATE_HOME="$OUT/home/.local/state" XDG_DATA_HOME="$OUT/home/.local/share" \
  DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent \
  dbus-run-session -- bash -c '
set -u
if [ -e "$XDG_RUNTIME_DIR/systemd" ]; then echo "refusing: live runtime" >&2; exit 3; fi
rm -rf "$HOME"; mkdir -p "$XDG_CONFIG_HOME/scroll"
# The client may name ~ paths (the Chromium checkout, depot_tools); link the
# real ones into the scratch home so they resolve. Isolation stays: /run/user
# is private, the compositor config is scratch, nothing is written back.
for d in chromium depot_tools; do
  [ -e "$REAL_HOME/$d" ] && ln -s "$REAL_HOME/$d" "$HOME/$d"
done
printf "output HEADLESS-1 resolution 1920x1080 scale 1\n" > "$XDG_CONFIG_HOME/scroll/config"
# The compositor alone gets the renderer choice; the client inherits none of it.
cenv=(WLR_BACKENDS=headless WLR_RENDERER="$BENCH_WLR_RENDERER" WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1)
[ -n "$BENCH_DRM_DEVICE" ] && cenv+=(WLR_RENDER_DRM_DEVICE="$BENCH_DRM_DEVICE")
env "${cenv[@]}" "$SCROLL" -c "$XDG_CONFIG_HOME/scroll/config" > "$OUT/scroll.log" 2>&1 & SP=$!
echo "renderer: WLR_RENDERER=$BENCH_WLR_RENDERER${BENCH_DRM_DEVICE:+ WLR_RENDER_DRM_DEVICE=$BENCH_DRM_DEVICE}; /dev/dri: $(ls /dev/dri 2>/dev/null | tr "\n" " ")" > "$OUT/renderer.txt"
for i in $(seq 1 200); do ls "$XDG_RUNTIME_DIR"/wayland-* >/dev/null 2>&1 && break; sleep 0.05; done
export WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v lock | head -1)")
export SCROLLSOCK=$(ls "$XDG_RUNTIME_DIR"/*-ipc.* 2>/dev/null | head -1); export SWAYSOCK="$SCROLLSOCK"
echo "compositor: $WAYLAND_DISPLAY $SCROLLSOCK scroll pid $SP" | tee "$OUT/summary.txt"
cat "$OUT/renderer.txt" | tee -a "$OUT/summary.txt"
"$SCROLLMSG" -t get_outputs -r > "$OUT/outputs.json" 2>/dev/null
echo "client: $*" | tee -a "$OUT/summary.txt"
t0=$(date +%s%N)
WAYLAND_DEBUG=1 "$@" > "$OUT/client.log" 2>&1 & CP=$!
for i in $(seq 1 3000); do grep -qE "wl_surface[#@][0-9]+\.attach" "$OUT/client.log" && break; sleep 0.005; done
t1=$(date +%s%N); echo "first attach after $(( (t1-t0)/1000000 )) ms" | tee -a "$OUT/summary.txt"
sleep "$CLIENT_SETTLE"
alive=0; kill -0 $CP 2>/dev/null && alive=1
echo "client alive at capture: $alive" | tee -a "$OUT/summary.txt"
# FD census of the client process tree (the tree_pids idea of measure.sh, from /proc/*/stat).
# One fds<tag>-<pid>.txt per process (fd, link target, and the peer path of a unix socket),
# then a "fd kinds" line over the whole tree, the split of "other", and one count per process.
fd_census() {
python3 - "$OUT" "$1" "$CP" <<'"'"'PY'"'"' | tee -a "$OUT/summary.txt"
import os, re, sys
out, tag, root = sys.argv[1], sys.argv[2], int(sys.argv[3])
kids = {}
for d in os.listdir("/proc"):
    if d.isdigit():
        try:
            st = open(f"/proc/{d}/stat").read()
        except OSError:
            continue
        ppid = int(st[st.rindex(")") + 2:].split()[1])
        kids.setdefault(ppid, []).append(int(d))
pids, todo = [], [root]
while todo:
    p = todo.pop(); pids.append(p); todo.extend(kids.get(p, []))
order = ["socket", "anon_inode:[eventfd]", "pipe", "memfd", "/dev/shm", "file", "other"]
total = dict.fromkeys(order, 0); other = {}; per = []
label = f" ({tag})" if tag else ""
for p in sorted(pids):
    try:
        comm = open(f"/proc/{p}/comm").read().strip()
        fds = sorted(os.listdir(f"/proc/{p}/fd"), key=int)
    except OSError:
        continue
    unix = {}
    try:
        for line in open(f"/proc/{p}/net/unix").read().splitlines()[1:]:
            f = line.split()
            unix[f[6]] = f[7] if len(f) > 7 else "(unnamed)"
    except OSError:
        pass
    rows = []
    for fd in fds:
        try:
            t = os.readlink(f"/proc/{p}/fd/{fd}")
        except OSError:
            continue
        m = re.match(r"socket:\[(\d+)\]", t)
        if m:
            k = "socket"; t += " unix " + unix[m.group(1)] if m.group(1) in unix else ""
        elif t == "anon_inode:[eventfd]":
            k = t
        elif t.startswith("pipe:"):
            k = "pipe"
        elif t.startswith("/memfd:"):
            k = "memfd"
        elif t.startswith("/dev/shm/"):
            k = "/dev/shm"
        elif t.startswith("/") and not t.startswith("/dev/"):
            k = "file"
        else:
            k = "other"; o = re.sub(r"\d+$", "N", t) if t.startswith("/dev/") else t
            other[o] = other.get(o, 0) + 1
        total[k] += 1; rows.append(f"{fd}\t{k}\t{t}")
    name = "fds-" + (tag + "-" if tag else "") + f"{p}.txt"
    with open(os.path.join(out, name), "w") as fh:
        fh.write(f"# pid {p} {comm}: {len(rows)} fds\n" + "\n".join(rows) + "\n")
    per.append(f"{p}:{comm}={len(rows)}")
print(f"fd kinds{label}: " + " ".join(f"{k}={total[k]}" for k in order))
print(f"fd other{label}: " + (" ".join(f"{k}={v}" for k, v in sorted(other.items(), key=lambda kv: -kv[1])) or "none"))
print(f"fd per process{label}: " + " ".join(per))
PY
}
[ "$CLIENT_FD_DUMP" = 1 ] && [ "$alive" = 1 ] && { fd_census ""; fd_t0=$(date +%s); }
"$SCROLLMSG" -t get_tree -r > "$OUT/tree.json" 2>/dev/null
grim "$OUT/shot.png" 2>>"$OUT/summary.txt" && echo "screenshot: $(stat -c %s "$OUT/shot.png") bytes" | tee -a "$OUT/summary.txt"
grim -t ppm "$OUT/shot.ppm" 2>/dev/null && python3 - "$OUT/shot.ppm" <<'"'"'PY'"'"' | tee -a "$OUT/summary.txt"
import sys
d = open(sys.argv[1], "rb").read()
# P6 header: magic, width height, maxval, then raw RGB
parts = d.split(b"\n", 3); w, h = map(int, parts[1].split()); px = parts[3]
seen = set(); n = w * h
for i in range(0, n, 97):  # sample every 97th pixel
    seen.add(px[3*i:3*i+3])
    if len(seen) > 64: break
print(f"colours: {len(seen)} (sampled {w}x{h})")
PY
echo "attaches: $(grep -c 'wl_surface[#@][0-9]*\.attach' "$OUT/client.log")" | tee -a "$OUT/summary.txt"
grep -oE "zwlr_layer_shell_v1[#@][0-9]+\.get_layer_surface|zwlr_layer_surface_v1[#@][0-9]+\.configure|xdg_wm_base[#@][0-9]+\.get_xdg_surface|xdg_surface[#@][0-9]+\.get_popup|xdg_surface[#@][0-9]+\.get_toplevel" "$OUT/client.log" | sort | uniq -c | tee -a "$OUT/summary.txt"
bash "$HERE/measure.sh" client 10 $CP "$OUT/measure.tsv" | tee -a "$OUT/summary.txt"
if [ "$CLIENT_FD_DUMP" = 1 ] && [ "$alive" = 1 ] && [ "$CLIENT_FD_RECHECK" -gt 0 ] 2>/dev/null; then
  left=$(( fd_t0 + CLIENT_FD_RECHECK - $(date +%s) )); [ "$left" -gt 0 ] && sleep "$left"
  if kill -0 $CP 2>/dev/null; then fd_census "recheck"; echo "fd recheck at ${CLIENT_FD_RECHECK}s after the first census" | tee -a "$OUT/summary.txt"
  else echo "fd recheck: client gone" | tee -a "$OUT/summary.txt"; fi
fi
kill $CP 2>/dev/null; wait $CP 2>/dev/null
kill $SP 2>/dev/null; wait $SP 2>/dev/null
[ "$alive" = 1 ] && [ -s "$OUT/shot.png" ]
' _ "$@"
