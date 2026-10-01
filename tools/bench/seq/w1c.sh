#!/usr/bin/env bash
# w1c bench sequence: restore out/views, FD census of views_shell --bar, the four-way GPU-fair
# comparison (open decision P23). Runs on the bench, outside the FHS environment, as the transient
# unit vs-w1c-seq under ~/views-bench/bench.lock (see tools/bench/README.md). Prints every rc.
#   bash <synced worktree>/tools/bench/seq/w1c.sh
set -uo pipefail
WT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$HOME/views-bench"; FHS="$B/build-env"; R="$B/results"; SRC="$HOME/chromium/src"
H="$WT/tools/bench/worker/headless.sh"
echo "== w1c start $(date -Is) worktree $WT"

# 1. Wire the tree (pristine reset, shell/, patches/series).
wire_out=$(bash "$WT/tools/bench/worker/wire.sh" "$WT" 2>&1); rc=$?
printf '%s\n' "$wire_out" | tail -5; echo "wire rc $rc"
case "$(printf '%s\n' "$wire_out" | tail -1)" in WIRE-OK*) ;; *) echo "w1c: wire failed, stopping"; exit 10;; esac

# 2. Restore out/views: the bench args file, gn clean when args drifted or siso state is present
#    (siso back to ninja needs it), gn gen, then both binaries. Idempotent.
"$FHS" -c "cd $SRC && if ! cmp -s $WT/tools/bench/args.views.gn out/views/args.gn || [ -e out/views/.siso_deps ]; then echo 'restore: args drifted or siso state, gn clean'; cp $WT/tools/bench/args.views.gn out/views/args.gn && gn clean out/views; fi && gn gen out/views && autoninja -C out/views views_shell views_examples"
rc=$?; echo "build rc $rc"
[ "$rc" = 0 ] || { echo "w1c: build failed, stopping"; exit 11; }
ls -l "$SRC/out/views/views_shell" "$SRC/out/views/views_examples"

# GL lines of a client log (WAYLAND_DEBUG lines start with "[") and the compositor's renderer lines.
gl_lines() {
  grep -v '^\[' "$1/client.log" | grep -iE 'gl|egl|angle|swiftshader|vulkan|dri|gpu|viz' | head -40 > "$1/gl.txt"
  grep -iE 'render|gles|egl|drm|pixman|vulkan' "$1/scroll.log" | head -20 > "$1/scroll-renderer.txt"
  echo "-- client GL lines ($1/gl.txt)"; cat "$1/gl.txt"
  echo "-- compositor renderer lines"; cat "$1/scroll-renderer.txt"
}
gpu_state() {
  for c in /sys/class/drm/card*/device; do
    [ -r "$c/gpu_busy_percent" ] && echo "gpu $c busy=$(cat "$c/gpu_busy_percent")% vram_used=$(( $(cat "$c/mem_info_vram_used" 2>/dev/null || echo 0) / 1048576 )) MB"
  done
}
SS="--use-gl=angle --use-angle=swiftshader"
run() {  # run <name> <env...> -- <client args>
  local name="$1"; shift; local envs=(); while [ "$1" != -- ]; do envs+=("$1"); shift; done; shift
  echo "== run $name: ${envs[*]} :: $*"; gpu_state
  env CLIENT_FD_DUMP=1 "${envs[@]}" bash "$H" "$R/$name" "$FHS" -c "$*"
  local rc=$?; echo "run $name rc $rc"; cat "$R/$name/summary.txt"; gl_lines "$R/$name"
}
GPU=(BENCH_RUNTIME_TEST="$B/bin/runtime-test-gpu" BENCH_RUNTIME_TEST_ARGS=--allow-dri BENCH_WLR_RENDERER=gles2 WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128)

# 3. Install the GPU variant of the wrapper on the bench only.
mkdir -p "$B/bin" && cp "$WT/tools/bench/worker/runtime-test-gpu" "$B/bin/runtime-test-gpu" && chmod +x "$B/bin/runtime-test-gpu"
echo "install rc $?"; "$B/bin/runtime-test-gpu" --allow-dri -- ls -l /dev/dri; echo "allow-dri ls rc $?"

# 4. The runs. FD census everywhere; the baseline also rechecks after 60 s to show growth.
run w1c-fd CLIENT_FD_RECHECK=60 -- "~/chromium/src/out/views/views_shell --ozone-platform=wayland $SS --bar"
run w1c-fd-examples CLIENT_FD_RECHECK=60 -- "~/chromium/src/out/views/views_examples --ozone-platform=wayland $SS"
run w1c-gles2-ss "${GPU[@]}" -- "~/chromium/src/out/views/views_shell --ozone-platform=wayland $SS --bar"
run w1c-gpu "${GPU[@]}" -- "~/chromium/src/out/views/views_shell --ozone-platform=wayland --bar"
run w1c-gpu-examples "${GPU[@]}" -- "~/chromium/src/out/views/views_examples --ozone-platform=wayland"
gpu_state
echo "== w1c done $(date -Is)"
