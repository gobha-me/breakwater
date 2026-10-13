#!/usr/bin/env bash
# Builds the one-class extension, then runs a clean first-time headless import
# N times with and without a pre-seeded extension_list.cfg.
#   run.sh <godot binary> [N]
set -uo pipefail
cd "$(dirname "$0")"
godot="${1:?usage: run.sh <godot binary> [N]}"
n="${2:-5}"
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build out >/dev/null || exit 2
for mode in clean preseeded; do
    for i in $(seq 1 "$n"); do
        rm -rf project/.godot
        if [[ $mode == preseeded ]]; then
            mkdir -p project/.godot && echo "res://bin/repro.gdextension" > project/.godot/extension_list.cfg
        fi
        "$godot" --headless --path project --import >/dev/null 2>&1
        echo "$mode run $i: exit $?"
    done
done
