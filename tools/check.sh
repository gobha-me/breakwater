#!/usr/bin/env bash
# Every gate a change must pass. Stops at the first failure.
set -euo pipefail
cd "$(dirname "$0")/.."

gate() { printf '\n=== %s\n' "$1"; }

gate "format";           tools/format.sh --check
gate "gdscript";         tools/gdlint.sh
gate "clang ASan+UBSan"; cmake --preset asan >/dev/null && cmake --build --preset asan && ctest --preset asan
gate "gcc";              cmake --preset gcc >/dev/null && cmake --build --preset gcc && ctest --preset gcc
gate "clang dev";        cmake --preset dev >/dev/null && cmake --build --preset dev && ctest --preset dev
gate "clang-tidy";       tools/lint.sh
gate "replay";           tools/replay_check.sh
gate "proof";            tools/seeds_check.sh
gate "godot smoke";      tools/godot_smoke.sh
# Rendering needs a display (an unlocked desktop session or xvfb-run). With
# none available, BREAKWATER_SKIP_GOLDEN=1 skips this gate, loudly.
if [[ "${BREAKWATER_SKIP_GOLDEN:-}" == 1 ]]; then
    gate "golden image";  echo "SKIPPED: BREAKWATER_SKIP_GOLDEN=1 (not verified)"
else
    gate "golden image";  tools/golden.sh
fi

printf '\nAll gates passed.\n'
