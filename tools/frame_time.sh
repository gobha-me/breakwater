#!/usr/bin/env bash
# Average GPU and CPU render time of the main scene over 300 frames, with vsync
# off. FPS is no use here: the compositor caps it at the display rate.
#   tools/frame_time.sh
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/display.sh
# A locked session suspends the window; time out instead of hanging.
timeout 90 "${display[@]}" "${GODOT_BIN:-godot}" --path game --resolution 1280x720 "${display_args[@]}" --disable-vsync \
    --script res://tests/frame_time.gd -- --no-hints 2>&1 | grep '^frame_time:'
