#!/usr/bin/env bash
# Format C++ sources. --check fails on any difference instead of rewriting.
set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t files < <(git ls-files --cached --others --exclude-standard -- 'core/*.[ch]pp' 'tests/*.[ch]pp' 'extension/*.[ch]pp')
if [[ "${1:-}" == "--check" ]]; then
    clang-format --dry-run --Werror "${files[@]}"
else
    clang-format -i "${files[@]}"
fi
