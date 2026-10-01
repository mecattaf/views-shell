#!/usr/bin/env bash
# wire-and-gen.sh — THE authoritative pristine wire + gn gen gate for agency-mvp.
#
# This is the single blessed entry point for the wire step of the Chromium dev
# loop (docs/WORKER-DEV-LOOP.md, docs/CHROMIUM-UPGRADE.md §2). Run it INSIDE the
# FHS sandbox on the worker (gn/autoninja must be on PATH).
#
# It ALWAYS resets the Chromium checkout to pristine first (protocol §2,
# non-negotiable: wire-agency.sh's git-apply fallback can double-apply on an
# already-wired tree and leave <<<<<<< conflict markers), then:
#
#   git reset --hard HEAD && git clean -fdq -e out   (pristine, keep out/)
#     -> build/wire-agency.sh --gen-only [--with-layer-shell]   (rsync + patch + gn gen)
#     -> re-append cc_wrapper="ccache"                           (LOAD-BEARING)
#     -> gn gen out/agency                                       (so ccache takes effect)
#     -> print the drift tally + the literal WIRE-AND-GEN-OK / -INCOMPLETE token
#
# NOTE vs. the legacy nix-chromium-builder/builder/wire-and-gen.sh: agency-mvp
# DROPPED browser-features/ entirely (zero references from src/agency/**), so the
# wire-browser-features.sh --all step is REMOVED here, not no-op'd — there is no
# browser-features overlay to wire in this repo (docs/DECISIONS.md DROP list).
#
# Success contract (documented verbatim in docs/WORKER-DEV-LOOP.md):
#   - literal token WIRE-AND-GEN-OK on the last line
#   - drift count == 0 (no "did not apply cleanly", no "disabling layer_shell",
#     no "patch context drifted") AND, structurally, zero --3way applies
#     (wire-agency.sh never uses --3way — it is git apply --check guarded)
#   - gn gen rc == 0
#   - when --layer-shell was requested, the final args.gn must carry
#     agency_enable_layer_shell = true (a silent downgrade to false is a FAIL)
#   - cc_wrapper = "ccache" present in the final args.gn
#
# Usage (from the coordinator, entering the FHS env on the worker):
#   flock /tmp/worker-tb.lock ssh -o BatchMode=yes worker-tb "bash -c '
#     /tmp/fhs-result/bin/agency-chromium-build-env -c \
#       \"bash ~/agency-src/build/wire-and-gen.sh --chromium ~/chromium/src --agency ~/agency-src --layer-shell\"'"
#
# Options:
#   --chromium <dir>   Chromium checkout       (default ~/chromium/src)
#   --agency   <dir>   agency source overlay   (default the repo this script is in)
#   --layer-shell      pass --with-layer-shell to wire-agency.sh (the shell path)
#   --out      <dir>   out dir, relative to chromium src (default out/agency)
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_AGENCY="$(cd "${SCRIPT_DIR}/.." && pwd)"

CHROMIUM_SRC="${HOME}/chromium/src"
AGENCY_SRC="${DEFAULT_AGENCY}"
LAYER_SHELL_FLAG=""
OUT_DIR="out/agency"
WIRE_LOG="/tmp/wire-and-gen.log"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --chromium) CHROMIUM_SRC="$2"; shift 2 ;;
    --agency)   AGENCY_SRC="$2"; shift 2 ;;
    --layer-shell) LAYER_SHELL_FLAG="--with-layer-shell"; shift ;;
    --out)      OUT_DIR="$2"; shift 2 ;;
    -h|--help)  grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

command -v gn >/dev/null 2>&1 || { echo "gn not on PATH — are you inside the FHS sandbox (/tmp/fhs-result/bin/agency-chromium-build-env)?"; exit 3; }

ARGS_FILE="${CHROMIUM_SRC}/${OUT_DIR}/args.gn"

echo "=== reset checkout to pristine (protocol §2) ==="
( cd "$CHROMIUM_SRC" && git reset --hard HEAD >/dev/null && git clean -fdq -e out )
echo "HEAD: $(cd "$CHROMIUM_SRC" && git rev-parse HEAD)  dirty=$(cd "$CHROMIUM_SRC" && git status --porcelain | wc -l)"

echo
echo "=== wire-agency.sh --gen-only ${LAYER_SHELL_FLAG} ==="
# tee to a log so we can tally drift signals below.
"${AGENCY_SRC}/build/wire-agency.sh" --chromium "$CHROMIUM_SRC" --out "$OUT_DIR" \
    --gen-only $LAYER_SHELL_FLAG 2>&1 | tee "$WIRE_LOG"

echo
echo "=== re-append cc_wrapper=ccache (LOAD-BEARING) + gn gen ==="
# The wire rewrites args.gn from build/args.gn every time and drops cc_wrapper.
# Without this line every subsequent build is cold (~10x slower).
if [[ -f "$ARGS_FILE" ]] && ! grep -q 'cc_wrapper' "$ARGS_FILE"; then
  printf '\ncc_wrapper = "ccache"\n' >> "$ARGS_FILE"
  echo "appended cc_wrapper=\"ccache\" to ${ARGS_FILE}"
else
  echo "cc_wrapper already present (or args.gn absent)"
fi
( cd "$CHROMIUM_SRC" && gn gen "$OUT_DIR" )
GEN_RC=$?

echo
echo "=== WIRE TALLY ==="
# wire-agency.sh is git-apply --check guarded; it NEVER uses --3way. Its real
# failure signals are the drift warnings below. Count them; also grep for the
# legacy "FAILED to apply"/"applied (--3way)" strings so the contract is literal
# even though wire-agency.sh cannot emit them (structurally always 0 here).
# NB: grep -c exits 1 on a 0 count — capture then default, never `|| echo 0`
# (that appends a second line and breaks the arithmetic below).
FAILED=$(grep -c "FAILED to apply" "$WIRE_LOG" 2>/dev/null || true); FAILED=${FAILED:-0}
THREEWAY=$(grep -c "applied (--3way)" "$WIRE_LOG" 2>/dev/null || true); THREEWAY=${THREEWAY:-0}
DRIFT=$(grep -cE "did not apply cleanly|disabling layer_shell for this gen|patch context drifted" "$WIRE_LOG" 2>/dev/null || true); DRIFT=${DRIFT:-0}
echo "FAILED to apply     : ${FAILED}"
echo "applied (--3way)    : ${THREEWAY}"
echo "wire-agency drift   : ${DRIFT}"
grep -E "did not apply cleanly|disabling layer_shell for this gen|patch context drifted|FAILED to apply|applied \(--3way\)" "$WIRE_LOG" 2>/dev/null || echo "(none — all patches applied strict)"
echo "gn gen rc           : ${GEN_RC}"

# Layer-shell must stay enabled when it was requested (a silent downgrade is a fail).
LS_OK=1
if [[ -n "$LAYER_SHELL_FLAG" ]]; then
  if grep -q '^agency_enable_layer_shell = true' "$ARGS_FILE" 2>/dev/null; then
    echo "agency_enable_layer_shell = true  (as requested)"
  else
    echo "agency_enable_layer_shell != true  (requested --layer-shell but got a downgrade)"
    LS_OK=0
  fi
fi

# cc_wrapper must be present in the final args.gn.
CC_OK=1
if grep -q 'cc_wrapper = "ccache"' "$ARGS_FILE" 2>/dev/null; then
  echo "cc_wrapper = \"ccache\"  (present)"
else
  echo "cc_wrapper missing from ${ARGS_FILE}"
  CC_OK=0
fi

echo
if [[ "$FAILED" -eq 0 && "$THREEWAY" -eq 0 && "$DRIFT" -eq 0 \
      && "$GEN_RC" -eq 0 && "$LS_OK" -eq 1 && "$CC_OK" -eq 1 ]]; then
  echo "WIRE-AND-GEN-OK"
  exit 0
else
  echo "WIRE-AND-GEN-INCOMPLETE"
  exit 1
fi
