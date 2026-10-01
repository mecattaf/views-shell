#!/usr/bin/env bash
# Run tools/validate.py, fetching python3 with jsonschema from nixpkgs when the
# local python lacks it.
set -euo pipefail
cd "$(dirname "$0")/.."
if python3 -c 'import jsonschema' 2>/dev/null; then
  exec python3 tools/validate.py
fi
exec nix shell --impure --expr \
  '(builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem}.python3.withPackages (p: [ p.jsonschema ])' \
  -c python3 tools/validate.py
