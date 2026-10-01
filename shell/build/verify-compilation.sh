#!/usr/bin/env bash
# verify-compilation.sh — machine-verifiable evidence that //agency compiled and
# LINKED against the real Chromium tree. Agents MUST run this after any
# compilation claim and paste its output into the PR.
#
# This exists because of a hard-learned lesson: "autoninja exits 0" is necessary
# but NOT sufficient. This script proves specific things instead:
#   - the //agency object files were actually produced (proof of compilation),
#   - the runnable chrome BINARY exists, is an ELF, and is NOT stale relative to
#     the newest agency object (proof of link + recency),
#   - a non-mutating `ninja -n` reprobe says "no work to do" (proof the tree is
#     self-consistent — this is what replaces trusting exit 0).
# Any failure prints FAIL and the script exits non-zero. It never self-reports.
#
# Fixed for agency-mvp (was hardcoded to out/Default + cowl/src paths, which the
# agency link-gate build at out/agency never populates — every check silently
# passed-vacuous or failed-spurious). Now: --out is a parameter (default
# out/agency), the object/artifact checks target the real agency band, and the
# cowl→agency layer-shell grep-evidence block is preserved.
#
# Usage (inside the FHS sandbox on the worker so gn/ninja are on PATH):
#   ./build/verify-compilation.sh [--chromium PATH] [--out out/agency]
#                                 [--expect-version X.Y.Z.W] [--no-reprobe]
#
# --no-reprobe skips the `ninja -n` gate (use only when ninja is not on PATH,
# e.g. running this from the coordinator over ssh outside the FHS env).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CHROMIUM_SRC="${HOME}/chromium/src"
OUT_DIR="out/agency"
EXPECT_VERSION=""
REPROBE=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --chromium)       CHROMIUM_SRC="$2"; shift 2 ;;
        --out)            OUT_DIR="$2"; shift 2 ;;
        --expect-version) EXPECT_VERSION="$2"; shift 2 ;;
        --no-reprobe)     REPROBE=0; shift ;;
        -h|--help)        grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

CHROMIUM_SRC="$(realpath -m "$CHROMIUM_SRC")"
OUT="${CHROMIUM_SRC}/${OUT_DIR}"
OBJ="${OUT}/obj"
HOST_DIR="${CHROMIUM_SRC}/ui/ozone/platform/wayland/host"

# Default expected version from build/CHROMIUM_VERSION if not overridden.
if [[ -z "$EXPECT_VERSION" && -f "${REPO_ROOT}/build/CHROMIUM_VERSION" ]]; then
    EXPECT_VERSION="$(tr -d '[:space:]' < "${REPO_ROOT}/build/CHROMIUM_VERSION")"
fi

ERRORS=0
CHECKS=0
pass() { echo "  PASS: $1"; CHECKS=$((CHECKS + 1)); }
fail() { echo "  FAIL: $1"; CHECKS=$((CHECKS + 1)); ERRORS=$((ERRORS + 1)); }

echo ""
echo "=================================================="
echo "        AGENCY COMPILATION VERIFICATION"
echo "=================================================="
echo "Chromium src: ${CHROMIUM_SRC}"
echo "Out dir:      ${OUT}"
echo "Timestamp:    $(date -Iseconds)"
echo ""

# --- Check 1: Chromium version ---
echo "-- Check 1: Chromium version --"
if [[ -f "${CHROMIUM_SRC}/chrome/VERSION" ]]; then
    MAJOR=$(grep 'MAJOR=' "${CHROMIUM_SRC}/chrome/VERSION" | cut -d= -f2)
    MINOR=$(grep 'MINOR=' "${CHROMIUM_SRC}/chrome/VERSION" | cut -d= -f2)
    BUILD=$(grep 'BUILD=' "${CHROMIUM_SRC}/chrome/VERSION" | cut -d= -f2)
    PATCH=$(grep 'PATCH=' "${CHROMIUM_SRC}/chrome/VERSION" | cut -d= -f2)
    VERSION="${MAJOR}.${MINOR}.${BUILD}.${PATCH}"
    if [[ -n "$EXPECT_VERSION" && "$VERSION" != "$EXPECT_VERSION" ]]; then
        fail "Chromium ${VERSION} != expected ${EXPECT_VERSION}"
    else
        pass "Chromium ${VERSION}${EXPECT_VERSION:+ (matches build/CHROMIUM_VERSION)}"
    fi
else
    fail "chrome/VERSION not found — not a Chromium checkout?"
fi

# --- Check 2: gn args evidence (the build was configured for agency) ---
echo ""
echo "-- Check 2: gn args evidence (${OUT_DIR}/args.gn) --"
ARGS_FILE="${OUT}/args.gn"
if [[ -f "$ARGS_FILE" ]]; then
    grep -q '^enable_agency = true'            "$ARGS_FILE" && pass "enable_agency = true"            || fail "enable_agency != true in args.gn"
    grep -q '^agency_enable_layer_shell = true' "$ARGS_FILE" && pass "agency_enable_layer_shell = true" || fail "agency_enable_layer_shell != true in args.gn"
    grep -q 'cc_wrapper = "ccache"'            "$ARGS_FILE" && pass "cc_wrapper = ccache (warm builds)" || fail "cc_wrapper missing — builds are cold"
else
    fail "args.gn not found in ${OUT} — has the tree been gen'd?"
fi

# --- Check 3: layer-shell weld sources deployed to the patched tree ---
# (grep-evidence block, carried from the cowl-era script and re-pathed to the
# agency host weld: these files are welded in by wire-agency.sh step 3b.)
echo ""
echo "-- Check 3: layer-shell weld sources deployed to Chromium tree --"
WELD_FILES=(
    wayland_layer_shell.h wayland_layer_shell.cc
    wayland_layer_shell_window.h wayland_layer_shell_window.cc
)
WELD_MISSING=0
for f in "${WELD_FILES[@]}"; do
    [[ -f "${HOST_DIR}/${f}" ]] || { fail "not welded: ui/ozone/platform/wayland/host/${f}"; WELD_MISSING=$((WELD_MISSING+1)); }
done
[[ $WELD_MISSING -eq 0 ]] && pass "${#WELD_FILES[@]} layer-shell weld sources present in ozone host dir"

# --- Check 4: agency object files compiled (proof of compilation) ---
echo ""
echo "-- Check 4: //agency object files compiled --"
# Canonical, always-present objects across the core band + the ozone weld.
# (Producer bands gated by agency_enable_pending_mojom_producers are NOT listed
# here — only objects that the link-gate group `agency` forces unconditionally.)
OBJ_FILES=(
    ui/ozone/platform/wayland/wayland/wayland_layer_shell.o
    ui/ozone/platform/wayland/wayland/wayland_layer_shell_window.o
    agency/ozone/layer_shell/layer_shell/wayland_layer_shell.o
    agency/ozone/layer_shell/layer_shell/wayland_layer_shell_window.o
    agency/shell/shell/shell_host.o
    agency/shell/shell/bar_view.o
    agency/shell/shell/clock_controller.o
    agency/shell/shell/shell_browser_main_extra_parts.o
    agency/shell/shell/launcher_panel.o
    agency/producers/clock/clock/clock_producer.o
)
OBJ_MISSING=0
NEWEST_OBJ_MTIME=0
for f in "${OBJ_FILES[@]}"; do
    if [[ -f "${OBJ}/${f}" ]]; then
        m=$(stat -c %Y "${OBJ}/${f}" 2>/dev/null || echo 0)
        [[ "$m" -gt "$NEWEST_OBJ_MTIME" ]] && NEWEST_OBJ_MTIME="$m"
    else
        fail "not compiled: ${OUT_DIR}/obj/${f}"
        OBJ_MISSING=$((OBJ_MISSING+1))
    fi
done
[[ $OBJ_MISSING -eq 0 ]] && pass "${#OBJ_FILES[@]} agency .o files compiled"

# --- Check 5: the runnable BINARY exists, is an ELF, and is not stale ---
echo ""
echo "-- Check 5: chrome binary (link artifact) --"
CHROME_BIN="${OUT}/chrome"
if [[ -x "$CHROME_BIN" ]]; then
    SIZE=$(stat -c %s "$CHROME_BIN" 2>/dev/null || echo 0)
    KIND=$(head -c 4 "$CHROME_BIN" | od -An -tx1 | tr -d ' ')   # 7f454c46 == ELF
    if [[ "$KIND" == "7f454c46" ]]; then
        pass "chrome is an ELF executable ($(( SIZE / 1024 / 1024 )) MiB)"
    else
        fail "chrome exists but is not an ELF (magic=${KIND})"
    fi
    if [[ "$SIZE" -lt 104857600 ]]; then   # < 100 MiB => not a real component-build chrome
        fail "chrome is only $(( SIZE / 1024 / 1024 )) MiB — implausibly small, suspect a broken link"
    fi
    # Staleness: the binary must be at least as new as the newest agency object,
    # else it was never relinked against the latest compile.
    BIN_MTIME=$(stat -c %Y "$CHROME_BIN" 2>/dev/null || echo 0)
    NOW=$(date +%s)
    AGE_H=$(( (NOW - BIN_MTIME) / 3600 ))
    if [[ "$NEWEST_OBJ_MTIME" -gt 0 && "$BIN_MTIME" -lt "$NEWEST_OBJ_MTIME" ]]; then
        fail "chrome ($(date -d @"$BIN_MTIME" -Iseconds)) is OLDER than the newest agency .o ($(date -d @"$NEWEST_OBJ_MTIME" -Iseconds)) — stale, never relinked"
    else
        pass "chrome is newer than every agency .o (built ${AGE_H}h ago)"
    fi
else
    fail "no runnable binary at ${CHROME_BIN} — the link never produced chrome"
fi

# --- Check 6: non-mutating reprobe (replaces 'trust ninja exit 0') ---
echo ""
echo "-- Check 6: ninja self-consistency reprobe (dry-run, non-mutating) --"
if [[ "$REPROBE" -eq 1 ]]; then
    if command -v ninja >/dev/null 2>&1 || command -v autoninja >/dev/null 2>&1; then
        # -n is a DRY RUN: it computes the plan and prints "no work to do" when
        # up to date, without touching the tree. Safe to run any time.
        REPROBE_OUT="$(cd "$CHROMIUM_SRC" && ninja -C "$OUT_DIR" -n agency chrome 2>&1)"
        echo "    ninja -n agency chrome: ${REPROBE_OUT}"
        if echo "$REPROBE_OUT" | grep -q 'no work to do'; then
            pass "ninja: no work to do — tree is self-consistent (no mtime staleness)"
        else
            fail "ninja reports pending work — the build is NOT up to date (binary may be stale)"
        fi
    else
        fail "ninja not on PATH — run inside the FHS env, or pass --no-reprobe to skip"
    fi
else
    echo "  (skipped: --no-reprobe)"
fi

# --- Summary ---
echo ""
echo "=================================================="
if [[ $ERRORS -eq 0 ]]; then
    echo "RESULT: ALL ${CHECKS} CHECKS PASSED"
    echo "=================================================="
    exit 0
else
    echo "RESULT: ${ERRORS} FAILURE(S) out of ${CHECKS} checks"
    echo "=================================================="
    exit 1
fi
