#!/usr/bin/env bash
# Proof gate (concept criterion 4): every offered seed is proven winnable with a
# witness that replays to a win, and the seeds the solver cannot prove are
# exactly the committed kRejectedSeeds (core/include/breakwater/seeds.hpp).
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset release >/dev/null
cmake --build --preset release --target bw_solve >/dev/null
build/release/tools/solve/bw_solve --check-offered
