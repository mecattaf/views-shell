#!/usr/bin/env bash
# One row of process-tree cost: measure.sh <label> <seconds> <root-pid> <results.tsv>
# PSS and RSS from smaps_rollup, CPU percent and context switches over the window, threads and fds.
set -u
label="$1"; dur="$2"; root="$3"; out="$4"
tree_pids(){ local p=$1; echo "$p"; for c in $(pgrep -P "$p" 2>/dev/null); do tree_pids "$c"; done; }
CLK=$(getconf CLK_TCK)
cpu0=0; ctx0=0; for p in $(tree_pids "$root" | sort -un); do
  read -r u s <<<"$(awk '{print $14,$15}' /proc/$p/stat 2>/dev/null)"; cpu0=$((cpu0+${u:-0}+${s:-0}))
  v=$(awk '/ctxt_switches/{s+=$2} END{print s+0}' /proc/$p/status 2>/dev/null); ctx0=$((ctx0+${v:-0})); done
sleep "$dur"
cpu1=0; ctx1=0; pss=0; rss=0; fds=0; thr=0; n=0
for p in $(tree_pids "$root" | sort -un); do
  read -r u s <<<"$(awk '{print $14,$15}' /proc/$p/stat 2>/dev/null)"; cpu1=$((cpu1+${u:-0}+${s:-0}))
  v=$(awk '/ctxt_switches/{s+=$2} END{print s+0}' /proc/$p/status 2>/dev/null); ctx1=$((ctx1+${v:-0}))
  pr=$(awk '/^Pss:/{s+=$2} END{print s+0}' /proc/$p/smaps_rollup 2>/dev/null); pss=$((pss+${pr:-0}))
  rr=$(awk '/^Rss:/{s+=$2} END{print s+0}' /proc/$p/smaps_rollup 2>/dev/null); rss=$((rss+${rr:-0}))
  t=$(awk '/^Threads:/{print $2}' /proc/$p/status 2>/dev/null); thr=$((thr+${t:-0}))
  f=$(ls /proc/$p/fd 2>/dev/null | wc -l); fds=$((fds+f)); n=$((n+1)); done
printf '%s\tdur=%s\tprocs=%s\tthreads=%s\tcpu%%=%s\tPSS_MB=%s\tRSS_MB=%s\tctxsw/s=%s\tFDs=%s\n' "$label" "$dur" "$n" "$thr" \
  "$(awk "BEGIN{printf \"%.2f\",(($cpu1-$cpu0)/$CLK)/$dur*100}")" "$(awk "BEGIN{printf \"%.1f\",$pss/1024}")" \
  "$(awk "BEGIN{printf \"%.1f\",$rss/1024}")" "$(awk "BEGIN{printf \"%.1f\",($ctx1-$ctx0)/$dur}")" "$fds" | tee -a "$out"
