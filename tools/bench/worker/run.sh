#!/usr/bin/env bash
# Unit body on the bench: run <cmd...> inside the FHS build environment, log and record the rc.
# usage: run.sh <name> <cmd...>
set -uo pipefail
name="$1"; shift
B="$HOME/views-bench"; mkdir -p "$B/logs" "$B/results"
{
  echo "=== $name start $(date -u +%FT%TZ): $*"
  "$B/build-env" -c "$*"
  rc=$?
  echo "=== $name end $(date -u +%FT%TZ) rc=$rc"
  echo "$rc" > "$B/results/$name.rc"
} >> "$B/logs/$name.log" 2>&1
exit "${rc:-1}"
