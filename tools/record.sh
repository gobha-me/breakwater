#!/usr/bin/env bash
# Record a mission log as video: the main scene plays the log at real-time
# speed in Godot's Movie Maker mode (fixed 30 fps, so the same log always
# gives the same frame count), showing each selection and shove preview the
# way a player would see them, with the game's sound. Output is MP4 when
# ffmpeg is installed.
#   tools/record.sh <log> [out.mp4]
set -euo pipefail
cd "$(dirname "$0")/.."

log="$(realpath "${1:?usage: tools/record.sh <log> [out.mp4]}")"
name="$(basename "${log%.log}")"
out="$(realpath -m "${2:-build/record/$name.mp4}")"
mkdir -p "$(dirname "$out")" build/record
avi="$PWD/build/record/$name.avi"
rm -f "$avi"

source tools/display.sh
"${display[@]}" "${GODOT_BIN:-godot}" --path game --resolution 1280x720 "${display_args[@]}" \
    --write-movie "$avi" --fixed-fps 30 -- \
    --autoplay="$log" --speed=1 --pace=0.6 --hold=1.5 --watchdog=900 2>&1 | grep -E '^autoplay:' || true
[[ -s "$avi" ]] || { echo "record: no video written" >&2; exit 1; }

frames="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 "$avi" 2>/dev/null || echo "?")"
if command -v ffmpeg >/dev/null; then
    # Movie Maker mixes the game's audio into the AVI; keep it.
    ffmpeg -loglevel error -y -i "$avi" -c:v libx264 -crf 23 -pix_fmt yuv420p -c:a aac -b:a 160k \
        -movflags +faststart "$out"
    rm -f "$avi"
else
    out="$avi"
fi
echo "record: $out ($frames frames at 30 fps)"
