#!/usr/bin/env bash
# Size and dependency census of one built target in one output directory.
#   census.sh <out> <gn-label> <binary> <report-file>
#   e.g. census.sh release //views_shell:views_shell views_shell ~/views-bench/results/w1b-census/release-views_shell.txt
# Run inside the FHS build environment (gn, ldd). Writes <report-file> and prints it:
#   size: as linked, and after llvm-strip (the checkout's clang toolchain copy,
#         third_party/llvm-build/Release+Asserts/bin/llvm-strip, which is not on PATH);
#   ldd: lines `ldd` prints (libraries plus the vdso and loader lines), and how many are not found;
#   runtime_deps: files `gn desc <label> runtime_deps` lists, and their total bytes;
#   deps: `gn desc <label> deps --all` total, then counts grouped by the first path
#         segment (//ui) and by the first two (//ui/views), largest first.
# Exit 1 when the binary is missing or gn fails.
set -uo pipefail
OUTNAME="${1:?usage: census.sh <out> <gn-label> <binary> <report-file>}"
LABEL="${2:?}"; BIN="${3:?}"; REPORT="${4:?}"
SRC="$HOME/chromium/src"; D="out/$OUTNAME"
STRIP="$SRC/third_party/llvm-build/Release+Asserts/bin/llvm-strip"
cd "$SRC" || exit 1
[ -x "$D/$BIN" ] || { echo "census: no binary $D/$BIN" >&2; exit 1; }
mkdir -p "$(dirname "$REPORT")"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
gn desc "$D" "$LABEL" deps --all > "$tmp/deps" 2>"$tmp/deps.err" || { cat "$tmp/deps.err" >&2; exit 1; }
gn desc "$D" "$LABEL" runtime_deps > "$tmp/rdeps" 2>"$tmp/rdeps.err" || { cat "$tmp/rdeps.err" >&2; exit 1; }
size=$(stat -c %s "$D/$BIN")
stripped=n/a
if [ -x "$STRIP" ]; then "$STRIP" -o "$tmp/stripped" "$D/$BIN" && stripped=$(stat -c %s "$tmp/stripped"); fi
lddn=$(ldd "$D/$BIN" | wc -l)
notfound=$(ldd "$D/$BIN" | grep -c 'not found')
rn=0; rbytes=0
while IFS= read -r f; do
  [ -n "$f" ] || continue; rn=$((rn+1))
  p="$D/$f"; if [ -d "$p" ]; then b=$(du -sb "$p" | cut -f1); else b=$(stat -L -c %s "$p" 2>/dev/null || echo 0); fi
  rbytes=$((rbytes+b))
done < "$tmp/rdeps"
# Labels: //a/b/c:t or //a/b/c:t(//toolchain). Path segments of the directory part only.
sed -E 's/\(.*\)$//; s/:.*$//' "$tmp/deps" | grep '^//' > "$tmp/dirs"
{
  echo "census $D $LABEL ($(date -u +%FT%TZ))"
  echo "size_bytes=$size stripped_bytes=$stripped"
  echo "ldd_libraries=$lddn ldd_not_found=$notfound"
  echo "runtime_deps_files=$rn runtime_deps_bytes=$rbytes"
  echo "deps_total=$(grep -c . "$tmp/deps")"
  echo "--- by first segment"
  awk -F/ '{print "//" $3}' "$tmp/dirs" | sort | uniq -c | sort -rn
  echo "--- by first two segments"
  awk -F/ '{s="//" $3; if (NF>3) s=s "/" $4; print s}' "$tmp/dirs" | sort | uniq -c | sort -rn
  echo "--- runtime_deps"
  cat "$tmp/rdeps"
} > "$REPORT"
cat "$REPORT"
