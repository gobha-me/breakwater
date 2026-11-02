#!/usr/bin/env bash
# Parse workflow policy and exercise unsafe inputs without builds or renders.
set -euo pipefail
cd "$(dirname "$0")/.."
python=(python3)
if command -v uvx >/dev/null; then
    python=(uvx --from PyYAML==6.0.3 python3)
fi
"${python[@]}" tools/workflow_check.py
"${python[@]}" tools/test_workflow_check.py
