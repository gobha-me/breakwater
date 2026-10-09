#!/usr/bin/env bash
# clang-tidy over every project translation unit, using the dev build's
# compile_commands.json (configure the dev preset first).
set -euo pipefail
cd "$(dirname "$0")/.."

[[ -f build/dev/compile_commands.json ]] || { echo "lint: run 'cmake --preset dev' first" >&2; exit 2; }
run-clang-tidy -quiet -p build/dev "$PWD/(core|tests|extension)/.*\.cpp$"
