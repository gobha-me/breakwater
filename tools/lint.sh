#!/usr/bin/env bash
# clang-tidy over every project translation unit, using the dev build's
# compile_commands.json (build the dev preset first: clang-tidy needs
# godot-cpp's generated headers to analyse extension/).
# RUN_CLANG_TIDY picks the binary, so CI can pin a version.
set -euo pipefail
cd "$(dirname "$0")/.."

[[ -f build/dev/compile_commands.json ]] || { echo "lint: run 'cmake --preset dev' first" >&2; exit 2; }
"${RUN_CLANG_TIDY:-run-clang-tidy}" -quiet -p build/dev "$PWD/(core|tests|extension)/.*\.cpp$"
