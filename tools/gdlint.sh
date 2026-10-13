#!/usr/bin/env bash
# GDScript lint and format gate: gdlint with ./gdlintrc, and gdformat at 120
# columns to match .clang-format. gdtoolkit is pinned and run through uvx.
#   tools/gdlint.sh           check (exit 1 on any finding or unformatted file)
#   tools/gdlint.sh --fix     reformat in place, then lint
set -euo pipefail
cd "$(dirname "$0")/.."

# uvx locally; CI installs the same pinned version with pipx and runs it
# from PATH.
if command -v uvx >/dev/null; then
    gdtoolkit=(uvx --from gdtoolkit==4.5.0)
else
    gdtoolkit=()
fi
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- 'game/*.gd')
if [[ "${1:-}" == "--fix" ]]; then
    "${gdtoolkit[@]}" gdformat --line-length 120 "${files[@]}"
else
    "${gdtoolkit[@]}" gdformat --line-length 120 --check "${files[@]}"
fi
"${gdtoolkit[@]}" gdlint "${files[@]}"
