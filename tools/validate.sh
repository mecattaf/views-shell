#!/usr/bin/env bash
# The repository checks, in order:
#   1. tools/check-fences.sh   the public-repository fence
#   2. tools/check-identity.py the extension id derived from the manifest key
#   3. tools/validate.py       the JSON schemas, examples and fixtures
#                              (python3 with jsonschema, fetched from nixpkgs when
#                              the local python lacks it)
#
# Stream contract: "fences: clean" is the only line on stdout, so
# `out=$(tools/validate.sh)` is exactly that line. The identity line, every
# validation line and the final VALIDATE-OK go to stderr, in that order, so on a
# terminal the whole report reads fences → identity → validation → VALIDATE-OK.
# Capture the full report with `tools/validate.sh 2>&1`.
# Any check failing fails this script (set -e) before VALIDATE-OK is printed.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! fences=$(tools/check-fences.sh); then
  printf '%s\n' "$fences"
  exit 1
fi
printf '%s\n' "$fences"

python3 tools/check-identity.py >&2

if python3 -c 'import jsonschema' 2>/dev/null; then
  python3 tools/validate.py >&2
else
  nix shell --impure --expr \
    '(builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem}.python3.withPackages (p: [ p.jsonschema ])' \
    -c python3 tools/validate.py >&2
fi
