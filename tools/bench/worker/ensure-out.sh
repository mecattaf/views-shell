#!/usr/bin/env bash
# Bring one Chromium output directory to a known configuration, idempotently.
#   ensure-out.sh <out> <args-file>
# Run inside the FHS build environment (gn from depot_tools on PATH), from any directory;
# it works in ~/chromium/src. Steps:
#   1. mkdir -p out/<out>;
#   2. if out/<out>/args.gn differs from <args-file>, or out/<out>/.siso_deps exists
#      (the directory was last built by siso), copy <args-file> over out/<out>/args.gn,
#      run `gn clean out/<out>` (mandatory from siso back to ninja) and remove any
#      remaining .siso_* state;
#   3. gn gen out/<out>;
#   4. print ENSURE-OK <out> (with "cleaned" or "unchanged"), or exit 1.
# A second run with the same args file only re-runs gn gen, which leaves a built tree built.
set -uo pipefail
OUTNAME="${1:?usage: ensure-out.sh <out> <args-file>}"
ARGS="${2:?usage: ensure-out.sh <out> <args-file>}"
SRC="$HOME/chromium/src"
[ -f "$ARGS" ] || { echo "ensure-out: no args file $ARGS" >&2; exit 1; }
command -v gn >/dev/null || { echo 'ensure-out: gn not on PATH (run inside the FHS build environment)' >&2; exit 1; }
cd "$SRC" || exit 1
D="out/$OUTNAME"
mkdir -p "$D" || exit 1
state=unchanged
if ! cmp -s "$ARGS" "$D/args.gn" || [ -e "$D/.siso_deps" ]; then
  state=cleaned
  echo "ensure-out: $D: args differ or siso state present; resetting"
  cp "$ARGS" "$D/args.gn" || exit 1
  if [ -e "$D/build.ninja" ] || [ -e "$D/.siso_deps" ]; then
    gn clean "$D" || { echo "ensure-out: gn clean $D failed" >&2; exit 1; }
  fi
  rm -rf "$D"/.siso_* 2>/dev/null
  cmp -s "$ARGS" "$D/args.gn" || cp "$ARGS" "$D/args.gn" || exit 1
fi
gn gen "$D" || { echo "ensure-out: gn gen $D failed" >&2; exit 1; }
echo "ENSURE-OK $OUTNAME $state"
