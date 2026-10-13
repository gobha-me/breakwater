#!/usr/bin/env bash
# Render the main scene and save a PNG via the game's own viewport.
#   tools/capture.sh [out.png] [frames] [-- extra game args, e.g. --log=… --after-turn=3]
#
# Godot reads back its own viewport, but it still needs a display to render
# into: headless mode draws nothing. In order of preference: xvfb-run (works
# with the screen locked), the current DISPLAY or WAYLAND_DISPLAY, or the
# desktop session's Wayland socket when run over SSH. A locked screen suspends
# the window, so that last option only works while the session is unlocked.
set -euo pipefail
cd "$(dirname "$0")/.."

out="$(realpath -m "${1:-captures/main.png}")"
frames="${2:-30}"
shift $(( $# < 2 ? $# : 2 ))
[[ "${1:-}" == "--" ]] && shift
extra=("$@")
limit="${CAPTURE_TIMEOUT:-60}"
mkdir -p "$(dirname "$out")"
rm -f "$out"

args=(-- --capture="$out" --capture-frames="$frames" "${extra[@]}")
source tools/display.sh
run=("${display[@]}" "${GODOT_BIN:-godot}" --path game --resolution 1280x720 "${display_args[@]}" "${args[@]}")

# One retry: a compositor occasionally fails to present the first window.
status=0
for attempt in 1 2; do
    status=0
    timeout "$limit" "${run[@]}" || status=$?
    [[ $status -eq 124 && $attempt -eq 1 ]] && { echo "capture: timed out, retrying once" >&2; continue; }
    break
done
if [[ $status -eq 124 ]]; then
    echo "capture: timed out after ${limit}s; a hidden or locked display suspends rendering, use xvfb-run" >&2
    exit 1
fi
[[ $status -eq 0 && -s "$out" ]] || { echo "capture: failed (exit $status)" >&2; exit 1; }
echo "capture: $out"
