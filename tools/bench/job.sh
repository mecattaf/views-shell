#!/usr/bin/env bash
# Transient user units on the bench: start, wait, status, log. See README.md.
set -uo pipefail
HOST="${BENCH_HOST:-worker}"
RUN="${BENCH_RUN:-}"   # path of worker/run.sh on the bench; default: beside this worktree's synced copy
verb="${1:-}"; name="${2:-}"
[ -n "$verb" ] && [ -n "$name" ] || { echo 'usage: job.sh start <name> <script> [args] | wait <name> [seconds] | status <name> | log <name> [lines]' >&2; exit 2; }
case "$name" in *[!A-Za-z0-9._-]*) echo "job.sh: bad name $name" >&2; exit 2;; esac
ssh_() { ssh -o BatchMode=yes "$HOST" "$@"; }
case "$verb" in
  start)
    script="${3:-}"; [ -n "$script" ] || { echo 'job.sh start: missing script' >&2; exit 2; }
    shift 3
    if [ -z "$RUN" ]; then
      ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
      BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD | tr '/' '-')"
      RUN="\$HOME/views-shell-wt/$BRANCH/tools/bench/worker/run.sh"
    fi
    args=$(printf '%q ' "$script" "$@")
    ssh_ "rm -f ~/views-bench/results/$name.rc; systemctl --user reset-failed vs-$name 2>/dev/null; systemd-run --user --unit=vs-$name --collect -p Environment=HOME=\$HOME -p 'Environment=PATH=/run/wrappers/bin:/etc/profiles/per-user/\$USER/bin:/run/current-system/sw/bin:/nix/var/nix/profiles/default/bin' /run/current-system/sw/bin/bash $RUN $name $args"
    ;;
  wait)
    limit="${3:-1200}"; t=0
    while [ "$t" -lt "$limit" ]; do
      state=$(ssh_ "systemctl --user is-active vs-$name 2>/dev/null; cat ~/views-bench/results/$name.rc 2>/dev/null")
      rc=$(printf '%s\n' "$state" | sed -n 2p)
      if [ -n "$rc" ]; then echo "job $name: rc $rc"; exit "$rc"; fi
      case "$(printf '%s\n' "$state" | sed -n 1p)" in active|activating) ;; *) sleep 5; rc=$(ssh_ "cat ~/views-bench/results/$name.rc 2>/dev/null"); [ -n "$rc" ] && { echo "job $name: rc $rc"; exit "$rc"; }; echo "job $name: unit gone, no result"; exit 125;; esac
      sleep 15; t=$((t+15))
    done
    echo "job $name: still running after ${limit}s"; exit 124
    ;;
  status)
    rc=$(ssh_ "cat ~/views-bench/results/$name.rc 2>/dev/null")
    if [ -z "$rc" ]; then echo "job $name: no result ($(ssh_ "systemctl --user is-active vs-$name 2>/dev/null"))"; exit 125; fi
    echo "job $name: rc $rc"; exit "$rc"
    ;;
  log)
    ssh_ "tail -n ${3:-40} ~/views-bench/logs/$name.log 2>/dev/null"
    ;;
  *) echo "job.sh: unknown verb $verb" >&2; exit 2;;
esac
