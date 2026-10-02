#!/usr/bin/env bash
# Produce (or check) the byte fixtures the C++ style kit must reproduce.
#   tools/theme-goldens.sh           write style/tokens/fixtures/<theme>.{resolved,mixer}.json
#   tools/theme-goldens.sh --check   regenerate into a scratch directory, diff against the
#                                    committed fixtures, exit 1 on any difference
#
# For every examples/themes/<theme>/colors.toml:
#   <theme>.resolved.json  every key Omarchy's own resolver prints with --all
#                          (tools/omarchy-theme-color, vendored unmodified, MIT), as one
#                          JSON object with sorted keys. shell/style/omarchy_cascade.cc must
#                          produce exactly this object.
#   <theme>.mixer.json     the layer-2 pins of style/theme-map.json applied to that palette:
#                          colour id name -> "#rrggbb" (lowercase), sorted. A key whose
#                          resolved value is not #rrggbb pins nothing. on_accent is derived
#                          (theme-map.json): background when the WCAG contrast of accent on
#                          background is at least that of accent on bright_foreground, else
#                          bright_foreground. shell/style/theme_mixer.cc must produce exactly
#                          this object.
# Both files are json.dumps(sort_keys=True, indent=2) plus a newline. The C++ tests compare
# canonical JSON (parsed and re-serialised), so whitespace is not part of the contract.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESOLVER="$ROOT/tools/omarchy-theme-color"
MAP="$ROOT/style/theme-map.json"
FIX="$ROOT/style/tokens/fixtures"
mode="${1:-write}"
case "$mode" in write|--check) ;; *) echo "usage: theme-goldens.sh [--check]" >&2; exit 2;; esac

out="$FIX"
if [ "$mode" = --check ]; then
  out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fi
mkdir -p "$out"

n=0
for colors in "$ROOT"/examples/themes/*/colors.toml; do
  theme="$(basename "$(dirname "$colors")")"
  # The resolver's own default path is under $HOME; --file keeps it away from any real theme.
  if ! resolved="$(HOME=/nonexistent bash "$RESOLVER" --file "$colors" --all)"; then
    echo "theme-goldens: the resolver failed on $theme" >&2; exit 1
  fi
  printf '%s\n' "$resolved" > "$out/$theme.resolver.tsv"
  python3 - "$MAP" "$out/$theme.resolver.tsv" "$out/$theme.resolved.json" "$out/$theme.mixer.json" <<'PY' || exit 1
import json, re, sys

map_path, tsv_path, resolved_path, mixer_path = sys.argv[1:5]
resolved = {}
for line in open(tsv_path).read().split("\n"):
    if not line:
        continue
    key, _, value = line.partition("\t")
    resolved[key] = value

def rgb(value):
    if not re.fullmatch(r"#[0-9A-Fa-f]{6}", value or ""):
        return None
    return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5))

def luminance(c):
    def lin(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)

values = dict(resolved)
accent, bg, bfg = (rgb(resolved.get(k)) for k in ("accent", "background", "bright_foreground"))
if accent and bg and bfg:
    values["on_accent"] = (resolved["background"] if contrast(accent, bg) >= contrast(accent, bfg)
                           else resolved["bright_foreground"])

pins = json.load(open(map_path))["layer2_pins"]
mixer = {}
for raw_key, ids in pins.items():
    if raw_key == "note":
        continue
    key = raw_key.split(" (", 1)[0]
    c = rgb(values.get(key))
    if c is None:
        continue
    for color_id in ids:
        mixer[color_id] = "#%02x%02x%02x" % c

for path, obj in ((resolved_path, resolved), (mixer_path, mixer)):
    with open(path, "w") as f:
        f.write(json.dumps(obj, sort_keys=True, indent=2) + "\n")
PY
  rm -f "$out/$theme.resolver.tsv"
  n=$((n + 1))
done

if [ "$mode" = --check ]; then
  if ! diff -ru "$FIX" "$out" >&2; then
    echo "theme-goldens: style/tokens/fixtures differ from the resolver's output; run tools/theme-goldens.sh" >&2
    exit 1
  fi
  echo "theme-goldens: ok ($n themes)"
else
  echo "theme-goldens: wrote $((2 * n)) fixtures for $n themes to style/tokens/fixtures"
fi
