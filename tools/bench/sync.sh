#!/usr/bin/env bash
# rsync this worktree to the bench and print the remote path.
set -euo pipefail
HOST="${BENCH_HOST:-worker}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD | tr '/' '-')"
REMOTE="views-shell-wt/$BRANCH"
ssh -o BatchMode=yes "$HOST" "mkdir -p ~/$REMOTE ~/views-bench/logs ~/views-bench/results"
rsync -a --delete --exclude .git "$ROOT/" "$HOST:$REMOTE/"
echo "/home/$(ssh -o BatchMode=yes "$HOST" whoami)/$REMOTE"
