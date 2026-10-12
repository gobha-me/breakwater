#!/usr/bin/env bash
# Golden-image gate (concept criterion 6): renders the opening board of the
# committed win log and compares it with tests/golden/opening-seed20.png.
#   tools/golden.sh           compare (exit 1 on a mismatch; diff in build/golden/)
#   tools/golden.sh --update  re-record the golden after an intended visual change
#
# Rendering needs a display; see tools/capture.sh. The comparison itself runs
# headless. Rendering is bit-exact between runs on the dev machine, so the
# limit is tight: 0.02% of pixels (about 180), each off by more than 24/255.
# A single engineer recoloured changes about 1,100.
set -euo pipefail
cd "$(dirname "$0")/.."

golden="tests/golden/opening-seed20.png"
mkdir -p build/golden
actual="$PWD/build/golden/opening-seed20.png"
tools/capture.sh "$actual" 30 -- --log="$PWD/tests/logs/win-seed20.log" --after-turn=0 >build/golden/capture.log 2>&1 ||
    { echo "golden: capture failed"; tail -3 build/golden/capture.log; exit 2; }

if [[ "${1:-}" == "--update" ]]; then
    mkdir -p "$(dirname "$golden")"
    cp "$actual" "$golden"
    echo "golden: updated $golden"
    exit 0
fi
"${GODOT_BIN:-godot}" --headless --path game --script res://tests/golden_compare.gd -- \
    --golden="$PWD/$golden" --actual="$actual" --diff="$PWD/build/golden/diff.png" \
    --tolerance=24 --max-share=0.0002 2>&1 | grep '^golden:'
