#!/bin/sh
# Smoke test for nhdcc-driver: boot the engine headless and assert a clean
# first-map dump.  Run from anywhere; paths are resolved to the repo root.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
bin="$here/nhdcc-driver"
data="$repo/dat"

if [ ! -x "$bin" ]; then
    echo "smoke: building driver..."
    make -C "$here" >/dev/null
fi

sess=$(mktemp -d)
out=$(mktemp)
trap 'rm -rf "$sess" "$out"' EXIT

# Walk one step east ('l'), then EOF ends the run.
DCC_KEYS=l timeout 30 "$bin" --session "$sess" --data "$data" </dev/null >"$out" 2>&1 \
    && rc=0 || rc=$?

events=$(wc -l <"$out" | tr -d ' ')
glyphs=$(grep -c '"cb":"shim_print_glyph"' "$out" || true)
askline=$(grep -cE '"cb":"shim_(nhgetch|nh_poskey)"' "$out" || true)

echo "smoke: exit=$rc events=$events glyphs=$glyphs input_prompts=$askline"

fail=0
[ "$rc" -eq 0 ]        || { echo "FAIL: nonzero exit ($rc)"; fail=1; }
[ "$glyphs" -gt 0 ]    || { echo "FAIL: no map glyphs drawn"; fail=1; }
[ "$askline" -gt 0 ]   || { echo "FAIL: engine never requested input"; fail=1; }

if [ "$fail" -eq 0 ]; then
    echo "smoke: PASS"
else
    echo "smoke: FAIL (last 5 events)"; tail -5 "$out"
    exit 1
fi
