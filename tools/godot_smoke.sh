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
