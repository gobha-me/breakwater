#!/usr/bin/env bash
# Format C++ sources. --check fails on any difference instead of rewriting.
# CLANG_FORMAT picks the binary, so CI can pin a version.
set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t files < <(git ls-files --cached --others --exclude-standard -- 'core/*.[ch]pp' 'tests/*.[ch]pp' 'extension/*.[ch]pp' 'tools/*.[ch]pp')
format="${CLANG_FORMAT:-clang-format}"
if [[ "${1:-}" == "--check" ]]; then
    "$format" --dry-run --Werror "${files[@]}"
else
    "$format" -i "${files[@]}"
fi
