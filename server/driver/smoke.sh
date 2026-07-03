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

# Phase 0 final task: state snapshot. Walking one step east ('l') should
# move x by exactly +1, leave y and hp/hpmax unchanged -- this is the
# Phase-0 "Definition of done" scenario verbatim. The starting room is
# randomly rolled, so 'l' occasionally bumps a wall immediately east
# (no move, no time passed); retry a few fresh sessions rather than treat
# that as a snapshot-decoding failure.
sess3=$(mktemp -d)
out3=$(mktemp)
trap 'rm -rf "$sess" "$out" "$sess2" "$out2" "$sess3" "$out3"' EXIT

fail3=1
attempt=0
while [ "$attempt" -lt 5 ] && [ "$fail3" -ne 0 ]; do
    attempt=$((attempt + 1))
    rm -rf "$sess3"; : >"$out3"
    DCC_SNAPSHOT=1 DCC_KEYS=l timeout 30 "$bin" --session "$sess3" --data "$data" \
        </dev/null >"$out3" 2>&1 && rc3=0 || rc3=$?

    snap1=$(grep '"cb":"__snapshot"' "$out3" | sed -n 1p)
    snap2=$(grep '"cb":"__snapshot"' "$out3" | sed -n 2p)
    x1=$(echo "$snap1" | grep -oE '"x":[0-9]+' | head -1 | cut -d: -f2)
    y1=$(echo "$snap1" | grep -oE '"y":[0-9]+' | head -1 | cut -d: -f2)
    hp1=$(echo "$snap1" | grep -oE '"hp":[0-9]+' | cut -d: -f2)
    x2=$(echo "$snap2" | grep -oE '"x":[0-9]+' | head -1 | cut -d: -f2)
    y2=$(echo "$snap2" | grep -oE '"y":[0-9]+' | head -1 | cut -d: -f2)
    hp2=$(echo "$snap2" | grep -oE '"hp":[0-9]+' | cut -d: -f2)

    echo "smoke(snapshot): attempt=$attempt exit=$rc3 before=(x=$x1,y=$y1,hp=$hp1) after=(x=$x2,y=$y2,hp=$hp2)"

    if [ "$rc3" -eq 0 ] && [ -n "$x1" ] && [ -n "$x2" ] \
        && [ "$x2" -eq $((x1 + 1)) ] && [ "$y2" -eq "$y1" ] && [ "$hp2" -eq "$hp1" ]; then
        fail3=0
    elif [ -n "$x1" ] && [ -n "$x2" ] && [ "$x2" -eq "$x1" ] && [ "$y2" -eq "$y1" ]; then
        echo "smoke(snapshot): bumped a wall (room shape), retrying with a fresh roll"
    else
        break # a real failure (bad exit, missing snapshot, unexpected delta) -- don't retry
    fi
done

if [ "$fail3" -eq 0 ]; then
    echo "smoke(snapshot): PASS"
else
    echo "smoke(snapshot): FAIL (last 10 events)"; tail -10 "$out3"
    exit 1
fi
