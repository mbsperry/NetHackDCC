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

# Phase 0 task 5: cmdq_add_ec injection. Queue a canned "inventory" extcmd
# instead of a real 'i' keypress and confirm the menu event sequence appears.
sess2=$(mktemp -d)
out2=$(mktemp)
trap 'rm -rf "$sess" "$out" "$sess2" "$out2"' EXIT

DCC_INJECT_INVENTORY=1 DCC_KEYS=l timeout 30 "$bin" --session "$sess2" --data "$data" \
    </dev/null >"$out2" 2>&1 && rc2=0 || rc2=$?

injected=$(grep -c '"cb":"__inject_inventory"' "$out2" || true)
menu=$(grep -c '"cb":"shim_add_menu"' "$out2" || true)

echo "smoke(inject): exit=$rc2 injected=$injected menu_items=$menu"

fail2=0
[ "$rc2" -eq 0 ]   || { echo "FAIL: nonzero exit ($rc2)"; fail2=1; }
[ "$injected" -eq 1 ] || { echo "FAIL: injection marker not seen exactly once"; fail2=1; }
[ "$menu" -gt 0 ]  || { echo "FAIL: no inventory menu items observed"; fail2=1; }

if [ "$fail2" -eq 0 ]; then
    echo "smoke(inject): PASS"
else
    echo "smoke(inject): FAIL (last 10 events)"; tail -10 "$out2"
    exit 1
fi
