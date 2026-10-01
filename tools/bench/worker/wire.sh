#!/usr/bin/env bash
# Wire a synced worktree into the Chromium checkout:
#   1. reset the checkout to pristine (keeps out/ and views_shell/);
#   2. copy <worktree>/shell/ to src/views_shell/ (rsync --delete);
#   3. apply every patch named in shell/patches/series with plain `git apply` (never --3way);
#   4. print WIRE-OK, or WIRE-FAILED <patch> and exit 1.
set -uo pipefail
WT="${1:?usage: wire.sh <worktree>}"
SRC="$HOME/chromium/src"
cd "$SRC" || exit 1
git checkout -q -- . && git clean -fdq -e out -e views_shell
rsync -a --delete "$WT/shell/" "$SRC/views_shell/"
series="$SRC/views_shell/patches/series"
if [ -f "$series" ]; then
  while read -r p; do
    case "$p" in ''|'#'*) continue;; esac
    if git apply --check "views_shell/patches/$p" 2>"$HOME/views-bench/logs/wire-$p.err"; then
      git apply "views_shell/patches/$p" && echo "applied $p"
    else
      echo "WIRE-FAILED $p"; cat "$HOME/views-bench/logs/wire-$p.err"; exit 1
    fi
  done < "$series"
fi
echo "WIRE-OK $(git rev-parse --short HEAD) $(git status --porcelain | wc -l) changed paths"
