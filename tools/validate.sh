#!/usr/bin/env bash
# The repository checks, in order:
#   1. tools/check-fences.sh   the public-repository fence
#   2. tools/check-identity.py the extension id derived from the manifest key
#   3. tools/validate.py       the JSON schemas, examples and fixtures
#                              (python3 with jsonschema, fetched from nixpkgs when
#                              the local python lacks it)
#   4. tools/plugin-conformance.py, once per T2 example program (echo-process,
#      power-menu, quick-settings-brightness): each prints CONFORMANCE-OK <id>
#
# Stream contract: "fences: clean" is the only line on stdout, so
# `out=$(tools/validate.sh)` is exactly that line. The identity line, every
# validation line, every conformance line and the final VALIDATE-OK go to stderr,
# in that order, so on a terminal the whole report reads fences → identity →
# validation → conformance → VALIDATE-OK.
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

# python3 with jsonschema: the local one when it has the package, else nixpkgs'.
if python3 -c 'import jsonschema' 2>/dev/null; then
  py=(python3)
else
  py=(nix shell --impure --expr
    '(builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem}.python3.withPackages (p: [ p.jsonschema ])'
    -c python3)
fi
"${py[@]}" tools/validate.py --defer-ok >&2

# The T2 example programs against the plugin-protocol conformance runner.
"${py[@]}" tools/plugin-conformance.py examples/echo-process >&2
"${py[@]}" tools/plugin-conformance.py examples/power-menu >&2
"${py[@]}" tools/plugin-conformance.py examples/quick-settings-brightness >&2

echo VALIDATE-OK >&2
