#!/usr/bin/env bash
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Hold the shared bench lock (~/views-bench/bench.lock on the bench) across many
# separate ssh commands, e.g. while tools/prove.sh re-runs bench rows that each
# ssh on their own and never take the lock themselves.
#
#   tools/bench/lock.sh hold      wait for the lock (flock -w 14400 on the bench),
#                                 print HOLDING and return, leaving a detached ssh
#                                 session that keeps it; exit 1 if it was not
#                                 obtained (timeout, ssh failure)
#   tools/bench/lock.sh release   end that session; the bench releases the lock
#   tools/bench/lock.sh status    report, read-only: `not held`, `held by pid N:
#                                 <command>` (from /proc/locks), and whether this
#                                 checkout's session is alive
#
# The holder is `setsid bash -c 'sleep infinity | ssh <bench> "flock -w 14400
# ~/views-bench/bench.lock sh -c \"echo HOLDING; read _\""'`: the remote shell
# waits on its stdin, which stays open while the local ssh lives. release kills
# the local process group (pgid in .bench-lock.pid at the repository root,
# gitignored; its output in .bench-lock.log, also gitignored), the channel
# closes, `read` sees end of input, and flock lets go. If the connection drops,
# the bench lets go the same way, so after a network fault check `status`.
#
# status runs `stat` on the lock file and reads /proc/locks; it never opens,
# creates or locks the file. hold creates the file if it does not exist (flock).
#
# A command that takes the lock itself (`flock ... bench.lock`, marked
# [takes bench.lock] by tools/prove.sh --dry-run) waits while this script
# holds it: release first, or run it outside the hold.
# The bench host is `worker`; override with BENCH_HOST. The lock path is
# ~/views-bench/bench.lock on that host; BENCH_LOCK overrides it, for testing
# this script against a scratch file (it is inserted into the remote command
# unquoted, so it must be a plain path).
set -uo pipefail
HOST="${BENCH_HOST:-worker}"
LOCK="${BENCH_LOCK:-~/views-bench/bench.lock}"
case "$LOCK" in *[!A-Za-z0-9._/~-]*) echo "lock.sh: bad BENCH_LOCK $LOCK" >&2; exit 2;; esac
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PIDF="$ROOT/.bench-lock.pid"
LOGF="$ROOT/.bench-lock.log"
WAIT=14400

alive() { [ -f "$PIDF" ] && kill -0 -- "-$(cat "$PIDF")" 2>/dev/null; }

case "${1:-}" in
  hold)
    if alive; then echo "HOLDING (already, pgid $(cat "$PIDF"))"; exit 0; fi
    rm -f "$PIDF"; : > "$LOGF"
    remote="flock -w $WAIT $LOCK sh -c 'echo HOLDING; read _'"
    setsid bash -c 'sleep infinity | exec ssh -o BatchMode=yes -o ServerAliveInterval=30 -o ServerAliveCountMax=4 "$1" "$2"' \
      lock-hold "$HOST" "$remote" >"$LOGF" 2>&1 </dev/null &
    pgid=$!
    echo "$pgid" > "$PIDF"
    while kill -0 "$pgid" 2>/dev/null; do
      if grep -q '^HOLDING$' "$LOGF"; then echo HOLDING; exit 0; fi
      sleep 1
    done
    echo "lock.sh: lock not obtained:" >&2
    cat "$LOGF" >&2
    rm -f "$PIDF"
    exit 1
    ;;
  release)
    if ! [ -f "$PIDF" ]; then echo "not holding"; exit 0; fi
    pgid=$(cat "$PIDF")
    kill -TERM -- "-$pgid" 2>/dev/null
    for _ in 1 2 3 4 5; do kill -0 -- "-$pgid" 2>/dev/null || break; sleep 1; done
    kill -KILL -- "-$pgid" 2>/dev/null
    rm -f "$PIDF"
    echo "RELEASED"
    ;;
  status)
    out=$(ssh -o BatchMode=yes "$HOST" "f=$LOCK"'
      [ -e "$f" ] || { echo "not held (no lock file)"; exit 0; }
      ino=$(stat -c %i "$f")
      holders=$(grep -E "^[0-9]+: FLOCK +[A-Z]+ +WRITE +[0-9]+ +[0-9a-f]+:[0-9a-f]+:$ino " /proc/locks | awk "{print \$5}")
      waiters=$(grep -cE "^[0-9]+: -> FLOCK .* [0-9a-f]+:[0-9a-f]+:$ino " /proc/locks)
      if [ -z "$holders" ]; then echo "not held"; else
        for p in $holders; do echo "held by pid $p: $(ps -o args= -p "$p" 2>/dev/null || echo "(pid not visible)")"; done
      fi
      [ "$waiters" -gt 0 ] && echo "waiting: $waiters"
      exit 0') || { echo "lock.sh: cannot reach $HOST" >&2; exit 2; }
    printf '%s\n' "$out"
    if alive; then echo "this checkout: holding (pgid $(cat "$PIDF"))"
    elif [ -f "$PIDF" ]; then echo "this checkout: stale $PIDF (session gone)"; fi
    ;;
  *) echo 'usage: lock.sh hold|release|status' >&2; exit 2 ;;
esac
