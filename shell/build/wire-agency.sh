#!/usr/bin/env bash
# Agency LINK-GATE wiring script.
#
# Wires the //agency additive tree into a Chromium checkout and drives the
# incremental compile of the link-gate group:
#
#     gn gen out/agency && autoninja -C out/agency agency
#
# What it does (all idempotent):
#   1. Copy this repo's src/agency/ -> <chromium>/src/agency/  (the //agency
#      label root; brave-style additive tree, no gclient DEPS entry needed).
#   2. Add "//agency:agency" to the root BUILD.gn gn_all group (via
#      patches/agency-build-gate.patch, grep-guarded so re-runs are safe).
#   3. Ensure the layer-shell prerequisite: the wlr_layer_shell_protocol /
#      ext_background_effect_protocol wayland_protocol() targets that
#      patches/agency-source.patch adds to //third_party/wayland-protocols. If
#      they are absent AND --with-layer-shell was not requested, gn-gen with
#      agency_enable_layer_shell=false so the core band still builds green.
#   3b. Under --with-layer-shell (and only when layer-shell stays enabled),
#      weld the substrate: copy the 4 layer_shell + 20 host_overlay protocol
#      bodies into <chromium>/ui/ozone/platform/wayland/host/, and vendor the
#      10 protocol XMLs into <chromium>/third_party/wayland-protocols/. These
#      are the build inputs patches/agency-source.patch's sources/deps assume.
#   4. Write out/agency/args.gn and run `gn gen` + `autoninja ... agency`.
#
# Usage:
#   ./build/wire-agency.sh                       # local: ~/chromium/src
#   ./build/wire-agency.sh --chromium <other-host>/chromium/src   # ds4
#   ./build/wire-agency.sh --with-layer-shell    # also apply agency-source.patch
#   ./build/wire-agency.sh --gen-only            # wire + gn gen, skip ninja
#   ./build/wire-agency.sh --dry-run
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PATCHES_DIR="${REPO_ROOT}/patches"

CHROMIUM_SRC="${HOME}/chromium/src"
OUT_DIR="out/agency"
DRY_RUN=false
GEN_ONLY=false
WITH_LAYER_SHELL=false

if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; RESET=''
fi
info()  { echo -e "${CYAN}[WIRE]${RESET}  $*"; }
ok()    { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
err()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; }
die()   { err "$@"; exit 1; }
run()   { if $DRY_RUN; then echo -e "  ${YELLOW}[DRY-RUN]${RESET} $*"; else "$@"; fi; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --chromium)        CHROMIUM_SRC="$2"; shift 2 ;;
        --out)             OUT_DIR="$2"; shift 2 ;;
        --with-layer-shell) WITH_LAYER_SHELL=true; shift ;;
        --gen-only)        GEN_ONLY=true; shift ;;
        --dry-run)         DRY_RUN=true; shift ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

CHROMIUM_SRC="$(realpath -m "$CHROMIUM_SRC")"
AGENCY_DEST="${CHROMIUM_SRC}/agency"

echo ""
echo -e "${BOLD}Agency link-gate wiring${RESET}"
echo -e "  Source:       ${REPO_ROOT}/src/agency"
echo -e "  Chromium src: ${CHROMIUM_SRC}"
echo -e "  Target:       ${AGENCY_DEST}"
echo -e "  Out dir:      ${CHROMIUM_SRC}/${OUT_DIR}"
echo ""

[[ -d "$CHROMIUM_SRC" ]]            || die "Chromium src not found: ${CHROMIUM_SRC}"
[[ -f "${CHROMIUM_SRC}/BUILD.gn" ]] || die "No BUILD.gn in ${CHROMIUM_SRC} -- not a Chromium checkout?"
command -v gn        >/dev/null 2>&1 || warn "gn not on PATH (is depot_tools sourced?)"
command -v autoninja >/dev/null 2>&1 || warn "autoninja not on PATH (is depot_tools sourced?)"

# --- Optional version check (mirrors deploy.sh) ---
if [[ -f "${REPO_ROOT}/build/CHROMIUM_VERSION" && -f "${CHROMIUM_SRC}/chrome/VERSION" ]]; then
    EXP="$(tr -d '[:space:]' < "${REPO_ROOT}/build/CHROMIUM_VERSION")"
    V="$(awk -F= '/MAJOR/{a=$2}/MINOR/{b=$2}/BUILD/{c=$2}/PATCH/{d=$2}END{print a"."b"."c"."d}' "${CHROMIUM_SRC}/chrome/VERSION")"
    if [[ "$V" == "$EXP" ]]; then ok "Chromium version matches: ${V}"; else warn "Chromium version ${V} != expected ${EXP}"; fi
fi

# --- Step 1: copy the additive tree to //agency ---
info "Step 1/4: sync src/agency -> ${AGENCY_DEST}"
if $DRY_RUN; then
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} rsync src/agency/ -> ${AGENCY_DEST}/"
else
    mkdir -p "$AGENCY_DEST"
    rsync -a --delete \
        --exclude '__pycache__/' --exclude '*.pyc' \
        "${REPO_ROOT}/src/agency/" "${AGENCY_DEST}/"
    ok "  synced $(find "${AGENCY_DEST}" -name BUILD.gn | wc -l) BUILD.gn files"
fi

# --- Step 2: add //agency:agency to gn_all (grep-guarded) ---
info "Step 2/4: wire //agency:agency into root gn_all"
if grep -q '"//agency:agency"' "${CHROMIUM_SRC}/BUILD.gn"; then
    ok "  already present in root BUILD.gn"
else
    if $DRY_RUN; then
        echo -e "  ${YELLOW}[DRY-RUN]${RESET} git apply patches/agency-build-gate.patch"
    elif ( cd "$CHROMIUM_SRC" && git apply --check "${PATCHES_DIR}/agency-build-gate.patch" 2>/dev/null ); then
        ( cd "$CHROMIUM_SRC" && git apply "${PATCHES_DIR}/agency-build-gate.patch" )
        ok "  applied agency-build-gate.patch"
    else
        # Context drift: fall back to a direct, minimal insertion into gn_all.
        warn "  patch context drifted; inserting //agency:agency directly"
        run python3 - "$CHROMIUM_SRC/BUILD.gn" <<'PY'
import sys, io
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
needle = '  group("gn_all") {\n'
if '"//agency:agency"' not in s and needle in s:
    anchor = s.index(needle) + len(needle)
    ins = '    # Agency (views-shell) link-gate additive tree.\n    deps += [ "//agency:agency" ]\n'
    s = s[:anchor] + ins + s[anchor:]
    io.open(p, "w", encoding="utf-8").write(s)
    print("inserted deps += //agency:agency into gn_all")
else:
    print("no insertion (already wired or gn_all not found)")
PY
    fi
fi

# --- Step 2b: dbus thread_linux visibility (grep-guarded) ---
# //agency/dbus depends on //components/dbus/thread_linux, whose source_set has a
# restricted visibility list. Add "//agency/*" so the shared D-Bus thread is
# reachable. Idempotent: skip if already granted.
info "Step 2b: grant //agency/* visibility on //components/dbus/thread_linux"
DBUS_VIS_BUILD="${CHROMIUM_SRC}/components/dbus/thread_linux/BUILD.gn"
if grep -q '"//agency/\*"' "$DBUS_VIS_BUILD" 2>/dev/null; then
    ok "  already present in thread_linux BUILD.gn"
elif $DRY_RUN; then
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} git apply patches/agency-dbus-visibility.patch"
elif ( cd "$CHROMIUM_SRC" && git apply --check "${PATCHES_DIR}/agency-dbus-visibility.patch" 2>/dev/null ); then
    ( cd "$CHROMIUM_SRC" && git apply "${PATCHES_DIR}/agency-dbus-visibility.patch" )
    ok "  applied agency-dbus-visibility.patch"
else
    warn "  patch context drifted; inserting //agency/* visibility directly"
    run python3 - "$DBUS_VIS_BUILD" <<'PY'
import sys, io
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
needle = '    "//components/dbus/*",\n'
if '"//agency/*"' not in s and needle in s:
    anchor = s.index(needle) + len(needle)
    s = s[:anchor] + '    "//agency/*",\n' + s[anchor:]
    io.open(p, "w", encoding="utf-8").write(s)
    print("inserted //agency/* visibility into thread_linux")
else:
    print("no insertion (already wired or anchor not found)")
PY
fi

# --- Step 3: layer-shell prerequisite ---
info "Step 3/4: layer-shell prerequisite check"
LAYER_SHELL_ARG="true"
# Coexistence guard: if the full cowl embedder is also deployed, cowl already
# owns //cowl/build:cowl_buildflags -> the SAME cowl/build/cowl_buildflags.h that
# //agency/build:cowl_buildflags emits, and welds layer-shell into the real
# wayland target itself. Building agency's layer_shell too would double-generate
# that header. Let cowl own the layer-shell path in that tree.
if [[ -f "${CHROMIUM_SRC}/cowl/build/BUILD.gn" ]] \
   && grep -q 'cowl_buildflags' "${CHROMIUM_SRC}/cowl/build/BUILD.gn" 2>/dev/null; then
    warn "  cowl embedder co-deployed (owns cowl/build:cowl_buildflags)"
    warn "  -> agency_enable_layer_shell=false (cowl owns the layer-shell path)"
    LAYER_SHELL_ARG="false"
elif grep -q 'wlr_layer_shell_protocol' "${CHROMIUM_SRC}/third_party/wayland-protocols/BUILD.gn" 2>/dev/null; then
    ok "  wlr_layer_shell_protocol present -> agency_enable_layer_shell=true"
elif $WITH_LAYER_SHELL; then
    info "  applying agency-source.patch (adds wayland_protocol targets)"
    if $DRY_RUN; then
        echo -e "  ${YELLOW}[DRY-RUN]${RESET} git apply patches/agency-source.patch"
    elif ( cd "$CHROMIUM_SRC" && git apply --check "${PATCHES_DIR}/agency-source.patch" 2>/dev/null ); then
        ( cd "$CHROMIUM_SRC" && git apply "${PATCHES_DIR}/agency-source.patch" )
        ok "  applied agency-source.patch"
        # Chain the layer-shell follow-on patches in strict wire order (matches
        # RUN-TOPBAR.md §1):
        #   agency-source -> agency-layershell-factory-fix -> agency-layershell-popup
        #     -> ozone-empty-opaque-region -> agency-layer-shell-weld -> agency-shell-host
        # factory-fix corrects the kPopup fallthrough (so kMenu/kTooltip reach
        # WaylandPopup with the layer parent); popup adds the As* down-cast, the
        # GetXdgParentWindow stop, and the XdgPopup::Initialize get_popup branch.
        # ozone-empty-opaque-region is NOT layer-shell-gated in principle, but it
        # only matters when a layer surface is present (an empty opaque region on
        # the bar's surface), so it lives inside this --with-layer-shell chain.
        # agency-layer-shell-weld welds BrowserWidget/agency:bar onto
        # wlr-layer-shell in desktop_window_tree_host_linux.cc; agency-shell-host
        # adds the chrome_browser_main.cc ShellHost hook + the core BUILD.gn deps.
        # Each is --check-guarded; a drift disables layer-shell for this gen
        # rather than leaving a half-applied tree.
        for _p in agency-layershell-factory-fix agency-layershell-popup \
                  ozone-empty-opaque-region agency-layer-shell-weld \
                  agency-shell-host; do
            if ( cd "$CHROMIUM_SRC" && git apply --check "${PATCHES_DIR}/${_p}.patch" 2>/dev/null ); then
                ( cd "$CHROMIUM_SRC" && git apply "${PATCHES_DIR}/${_p}.patch" )
                ok "  applied ${_p}.patch"
            else
                warn "  ${_p}.patch did not apply cleanly; disabling layer_shell for this gen"
                LAYER_SHELL_ARG="false"
                break
            fi
        done
    else
        warn "  agency-source.patch did not apply cleanly; disabling layer_shell for this gen"
        LAYER_SHELL_ARG="false"
    fi
else
    warn "  wlr_layer_shell_protocol absent and --with-layer-shell not set"
    warn "  -> agency_enable_layer_shell=false (core band still builds green)"
    LAYER_SHELL_ARG="false"
fi

# --- Step 3b: weld layer-shell + protocol sources into the patched tree ---
# patches/agency-source.patch lists host/wayland_layer_shell*.{cc,h} +
# host/wayland_*.{cc,h} as //ui/ozone/platform/wayland sources and points the
# wayland_protocol() targets at third_party/wayland-protocols XMLs. Those are
# additive build inputs that live in this repo (src/agency/ozone/*), so place
# them at their patched-tree destinations. Only when the caller opted into
# layer-shell AND it stays enabled for this gen (skip when disabled / when the
# cowl embedder co-owns the weld, to avoid double-welding the same objects).
info "Step 3b: weld layer-shell + protocol sources"
LAYER_SHELL_SRC="${REPO_ROOT}/src/agency/ozone/layer_shell"
HOST_OVERLAY_SRC="${REPO_ROOT}/src/agency/ozone/host_overlay"
PROTOCOL_XML_SRC="${REPO_ROOT}/src/agency/ozone/protocol_xml"
HOST_DEST="${CHROMIUM_SRC}/ui/ozone/platform/wayland/host"
WLP_DEST="${CHROMIUM_SRC}/third_party/wayland-protocols"
if ! $WITH_LAYER_SHELL; then
    ok "  --with-layer-shell not set; skipping source weld"
elif [[ "$LAYER_SHELL_ARG" != "true" ]]; then
    warn "  layer-shell disabled for this gen; skipping source weld"
elif $DRY_RUN; then
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} rsync ${LAYER_SHELL_SRC}/wayland_layer_shell*.{cc,h} -> ${HOST_DEST}/"
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} rsync ${HOST_OVERLAY_SRC}/*.{cc,h} -> ${HOST_DEST}/"
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} rsync ${PROTOCOL_XML_SRC}/{staging,unstable} -> ${WLP_DEST}/"
else
    # (a) substrate (4) + protocol bodies (20) -> patched wayland host/ dir.
    mkdir -p "$HOST_DEST"
    rsync -a "${LAYER_SHELL_SRC}/"wayland_layer_shell*.cc \
             "${LAYER_SHELL_SRC}/"wayland_layer_shell*.h \
             "${HOST_OVERLAY_SRC}/"*.cc "${HOST_OVERLAY_SRC}/"*.h \
             "${HOST_DEST}/"
    ok "  welded $(ls "${LAYER_SHELL_SRC}/"wayland_layer_shell*.{cc,h} "${HOST_OVERLAY_SRC}/"*.{cc,h} | wc -l) host sources -> ui/ozone/platform/wayland/host/"
    # (b) protocol XMLs -> third_party/wayland-protocols/ (preserve staging/
    #     + unstable/ relative paths; NO --delete, upstream protocols coexist).
    rsync -a "${PROTOCOL_XML_SRC}/staging" "${PROTOCOL_XML_SRC}/unstable" "${WLP_DEST}/"
    ok "  vendored $(find "${PROTOCOL_XML_SRC}" -name '*.xml' | wc -l) protocol XMLs -> third_party/wayland-protocols/"
fi

# --- Step 4: gn gen + autoninja ---
info "Step 4/4: gn gen ${OUT_DIR} && autoninja agency"
ARGS_FILE="${CHROMIUM_SRC}/${OUT_DIR}/args.gn"
if $DRY_RUN; then
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} write ${ARGS_FILE} (enable_agency, agency_enable_layer_shell=${LAYER_SHELL_ARG})"
    echo -e "  ${YELLOW}[DRY-RUN]${RESET} (cd ${CHROMIUM_SRC} && gn gen ${OUT_DIR})"
    ${GEN_ONLY} || echo -e "  ${YELLOW}[DRY-RUN]${RESET} (cd ${CHROMIUM_SRC} && autoninja -C ${OUT_DIR} agency)"
    exit 0
fi

mkdir -p "$(dirname "$ARGS_FILE")"
{
    cat "${REPO_ROOT}/build/args.gn"
    echo ""
    echo "# --- appended by wire-agency.sh ---"
    echo "agency_enable_layer_shell = ${LAYER_SHELL_ARG}"
} > "$ARGS_FILE"
ok "  wrote ${ARGS_FILE}"

( cd "$CHROMIUM_SRC" && gn gen "$OUT_DIR" )
ok "  gn gen succeeded"

if $GEN_ONLY; then
    ok "gen-only: skipping ninja. Build with: (cd ${CHROMIUM_SRC} && autoninja -C ${OUT_DIR} agency)"
    exit 0
fi

( cd "$CHROMIUM_SRC" && autoninja -C "$OUT_DIR" agency )
ok "autoninja -C ${OUT_DIR} agency SUCCEEDED -- the link-gate compiles."
