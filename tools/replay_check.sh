#!/usr/bin/env bash
# Determinism gate (concept criterion 3): every committed mission log replays
# to byte-identical output under Clang and GCC, debug and release, and that
# output matches the .hashes file committed beside the log.
#   tools/replay_check.sh [preset ...]     (default: dev release gcc gcc-release)
set -euo pipefail
cd "$(dirname "$0")/.."

presets=("$@")
[[ ${#presets[@]} -gt 0 ]] || presets=(dev release gcc gcc-release)
mapfile -t logs < <({ find tests/logs -maxdepth 1 -name '*.log'; find tests/logs/campaigns -name '*.log' 2>/dev/null; } | sort)
[[ ${#logs[@]} -gt 0 ]] || { echo "replay: no logs in tests/logs" >&2; exit 2; }

failures=0
for preset in "${presets[@]}"; do
    cmake --preset "$preset" >/dev/null
    cmake --build --preset "$preset" --target bw_replay >/dev/null
    for log in "${logs[@]}"; do
        expected="${log%.log}.hashes"
        if diff -u "$expected" <("build/$preset/tools/replay/bw_replay" "$log") >/dev/null; then
            printf '  ok   %-12s %s\n' "$preset" "$log"
        else
            printf '  FAIL %-12s %s\n' "$preset" "$log"
            diff -u "$expected" <("build/$preset/tools/replay/bw_replay" "$log") || true
            failures=$((failures + 1))
        fi
    done
done
echo "replay: $((${#presets[@]} * ${#logs[@]} - failures))/$((${#presets[@]} * ${#logs[@]})) identical"
[[ $failures -eq 0 ]]
