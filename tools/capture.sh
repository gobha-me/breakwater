#!/usr/bin/env bash
# Render the main scene and save a PNG via the game's own viewport.
#   tools/capture.sh [out.png] [frames]
#
# Prefers a virtual X display (xvfb-run) so captures work over SSH and with the
# screen locked: a hidden Wayland window is suspended by the compositor and
# never draws. Falls back to the current display when xvfb-run is missing.
set -euo pipefail
cd "$(dirname "$0")/.."

out="$(realpath -m "${1:-captures/main.png}")"
frames="${2:-30}"
limit="${CAPTURE_TIMEOUT:-60}"
mkdir -p "$(dirname "$out")"
rm -f "$out"

godot=("${GODOT_BIN:-godot}" --path game --resolution 1280x720)
args=(-- --capture="$out" --capture-frames="$frames")

if command -v xvfb-run >/dev/null; then
    run=(xvfb-run -a -s "-screen 0 1280x720x24" "${godot[@]}" --display-driver x11 "${args[@]}")
elif [[ -n "${WAYLAND_DISPLAY:-}" || -n "${DISPLAY:-}" ]]; then
    run=("${godot[@]}" "${args[@]}")
else
    echo "capture: no display; install xvfb-run (Arch: xorg-server-xvfb)" >&2
    exit 2
fi

status=0
timeout "$limit" "${run[@]}" || status=$?
if [[ $status -eq 124 ]]; then
    echo "capture: timed out after ${limit}s; a hidden or locked display suspends rendering, use xvfb-run" >&2
    exit 1
fi
[[ $status -eq 0 && -s "$out" ]] || { echo "capture: failed (exit $status)" >&2; exit 1; }
echo "capture: $out"
