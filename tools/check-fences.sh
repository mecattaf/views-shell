#!/usr/bin/env bash
# Public-repository fence: no private paths, hosts, addresses or note names in the tree.
# Exit 0 and print "fences: clean" when nothing matches; otherwise print the hits and exit 1.
set -uo pipefail
cd "$(dirname "$0")/.."
pattern='/home/[a-z]+|~/mecattaf|~/agency-agency|~/Downloads|/mnt/nas|notes[0-9]+/|wm-scoping|\.wt/|leger\.run|[a-z0-9._-]+@(gmail|mecattaf|leger|agency)|10\.4[0-9]\.[0-9]+\.[0-9]+|/run/agenix|\bCUBS\b'
hits=$(git ls-files -z | xargs -0 grep -nIE "$pattern" -- 2>/dev/null | grep -v '^tools/check-fences.sh:' || true)
if [ -n "$hits" ]; then echo "$hits"; echo "fences: $(printf '%s\n' "$hits" | wc -l) hits"; exit 1; fi
echo "fences: clean"
