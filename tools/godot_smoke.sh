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
# The first import after another Godot build wrote .godot/ can abort once at
# exit (docs/KNOWN_ISSUES.md); a second attempt has always passed.
"$GODOT" --headless --path game --import >/dev/null 2>&1 ||
    "$GODOT" --headless --path game --import >/dev/null 2>&1
"$GODOT" --headless --path game --script res://tests/smoke.gd

# Script errors, engine errors and leaks at exit all fail the gate: a
# tolerated warning hides the next real one.
problems='SCRIPT ERROR|^ERROR|leaked at exit|Leaked instance'

# Every committed log, played command by command through the full main scene
# (presentation and event playback included), must end on its committed hash
# and print no script errors.
status=0
for log in tests/logs/*.log; do
    expected="$(grep '^end ' "${log%.log}.hashes" | tail -1 | cut -d' ' -f3)"
    outcome="$(grep '^outcome ' "${log%.log}.hashes" | cut -d' ' -f2)"
    out="$(timeout 180 "$GODOT" --headless --path game -- --autoplay="$PWD/$log" 2>&1)" || true
    if grep -qE "$problems" <<<"$out"; then
        echo "  FAIL autoplay $log: errors or leaks"; grep -E "$problems" <<<"$out" | head -5; status=1
    elif grep -q "outcome $outcome, hash $expected" <<<"$out"; then
        echo "  ok   autoplay $log: $outcome, $expected"
    else
        echo "  FAIL autoplay $log: expected $outcome $expected"; tail -3 <<<"$out"; status=1
    fi
done

# Every committed campaign, played mission by mission through the scene, must
# reach the score and the ending its .hashes record.
for log in tests/logs/campaigns/*.log; do
    [[ -e "$log" ]] || continue
    score="$(grep '^campaign score ' "${log%.log}.hashes" | cut -d' ' -f3)"
    ending="$(grep -E '^campaign (finished|unfinished)$' "${log%.log}.hashes" | cut -d' ' -f2)"
    out="$(timeout 300 "$GODOT" --headless --path game -- --autoplay="$PWD/$log" 2>&1)" || true
    if grep -qE "$problems" <<<"$out"; then
        echo "  FAIL autoplay $log: errors or leaks"; grep -E "$problems" <<<"$out" | head -5; status=1
    elif grep -qE "^autoplay: campaign [0-9]+, [0-9]+ missions, score $score, $ending$" <<<"$out"; then
        echo "  ok   autoplay $log: score $score, $ending"
    else
        echo "  FAIL autoplay $log: expected score $score, $ending"; tail -3 <<<"$out"; status=1
    fi
done

# Input: play whole missions with injected mouse clicks and keys, save each
# game the way F5 does, and require bw_replay to reach the hash the game showed.
replay="build/dev/tools/replay/bw_replay"
mkdir -p build && played="$(mktemp -d "$PWD/build/played.XXXXXX")"
trap 'rm -rf "$played"' EXIT
out="$(timeout 300 "$GODOT" --headless --path game --script res://tests/input_test.gd -- \
    --save-dir="$played" --seeds=20,19 --play-logs="$PWD/tests/logs/win-seed20.log" \
    --play-campaign="$PWD/tests/logs/campaigns/campaign-0.log" --no-hints 2>&1)" || status=1
grep -E '^  (ok|FAIL)|^input:' <<<"$out" || true
if grep -qE "$problems" <<<"$out" || ! grep -q '^input: PASS' <<<"$out"; then
    echo "  FAIL input test did not pass:"; grep -E "$problems|FAIL" <<<"$out" | head -5; status=1
fi
while read -r line; do
    shown="$(sed -E 's/.*hash ([0-9a-f]{16}).*/\1/' <<<"$line")"
    log="$(sed -E 's/.*log (.*)$/\1/' <<<"$line")"
    replayed="$("$replay" "$log" | grep '^end ' | tail -1 | cut -d' ' -f3)"
    if [[ "$shown" == "$replayed" ]]; then
        echo "  ok   saved game replays to the hash shown: ${line%%, hash*}"
    else
        echo "  FAIL saved game $log: game showed $shown, bw_replay reached $replayed"; status=1
    fi
done < <(grep '^played:' <<<"$out")
while read -r line; do
    shown="$(sed -E 's/.*score ([0-9]+),.*/\1/' <<<"$line")"
    log="$(sed -E 's/.*log (.*)$/\1/' <<<"$line")"
    replayed="$("$replay" "$log" | grep '^campaign score ' | cut -d' ' -f3)"
    if [[ "$shown" == "$replayed" ]]; then
        echo "  ok   saved campaign replays to the score shown: ${line%%, log*}"
    else
        echo "  FAIL saved campaign $log: game showed $shown, bw_replay scored $replayed"; status=1
    fi
done < <(grep '^played-campaign:' <<<"$out")
exit $status
