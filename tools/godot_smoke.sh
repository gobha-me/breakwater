#!/usr/bin/env bash
# Headless: import the project (registers the extension), then run the smoke test.
set -euo pipefail
cd "$(dirname "$0")/.."

GODOT="${GODOT_BIN:-godot}"
[[ -f game/bin/libbreakwater.linux.template_debug.x86_64.so ]] ||
    { echo "smoke: extension not built; run 'cmake --build --preset dev'" >&2; exit 2; }

# Pre-register the extension so it loads at startup. Godot 4.7.2 (CachyOS build)
# segfaults at exit when a first-time import discovers the extension mid-scan;
# see docs/KNOWN_ISSUES.md.
mkdir -p game/.godot
grep -qxF "res://bin/breakwater.gdextension" game/.godot/extension_list.cfg 2>/dev/null ||
    echo "res://bin/breakwater.gdextension" >> game/.godot/extension_list.cfg
"$GODOT" --headless --path game --import >/dev/null 2>&1
"$GODOT" --headless --path game --script res://tests/smoke.gd

# Every committed log, played command by command through the full main scene
# (presentation and event playback included), must end on its committed hash
# and print no script errors.
status=0
for log in tests/logs/*.log; do
    expected="$(grep '^end ' "${log%.log}.hashes" | tail -1 | cut -d' ' -f3)"
    outcome="$(grep '^outcome ' "${log%.log}.hashes" | cut -d' ' -f2)"
    out="$(timeout 180 "$GODOT" --headless --path game -- --autoplay="$PWD/$log" 2>&1)" || true
    if grep -qE 'SCRIPT ERROR|^ERROR' <<<"$out"; then
        echo "  FAIL autoplay $log: errors"; grep -E 'SCRIPT ERROR|^ERROR' <<<"$out" | head -5; status=1
    elif grep -q "outcome $outcome, hash $expected" <<<"$out"; then
        echo "  ok   autoplay $log: $outcome, $expected"
    else
        echo "  FAIL autoplay $log: expected $outcome $expected"; tail -3 <<<"$out"; status=1
    fi
done
exit $status
