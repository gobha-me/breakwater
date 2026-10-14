#!/usr/bin/env bash
# Play Breakwater on this desktop: builds what's missing, then launches.
#   tools/play.sh [seed]          a mission (default 20; rejected seeds move on)
#   tools/play.sh --campaign K    campaign K (0-199): five missions, scored
#   tools/play.sh --exported      the exported release build (tools/export.sh)
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${1:-}" == "--exported" ]]; then
    [[ -x build/export/breakwater.x86_64 ]] || tools/export.sh
    exec build/export/breakwater.x86_64 "${@:2}"
fi

game_args=(--seed="${1:-20}")
[[ "${1:-}" == "--campaign" ]] && game_args=(--campaign="${2:?usage: tools/play.sh --campaign K}")
[[ -f build/dev/build.ninja ]] || cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null
# Register the extension before Godot's first import (docs/KNOWN_ISSUES.md).
mkdir -p game/.godot
grep -qxF "res://bin/breakwater.gdextension" game/.godot/extension_list.cfg 2>/dev/null ||
    echo "res://bin/breakwater.gdextension" >> game/.godot/extension_list.cfg
[[ -f game/.godot/uid_cache.bin ]] || "${GODOT_BIN:-godot}" --headless --path game --import >/dev/null 2>&1 || true
exec "${GODOT_BIN:-godot}" --path game -- "${game_args[@]}"
