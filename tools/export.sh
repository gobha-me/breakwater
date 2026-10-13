#!/usr/bin/env bash
# Export a standalone Linux release build to build/export/, using the official
# Godot 4.7.2 export templates (downloaded once, verified against the release's
# SHA512-SUMS.txt, cached in ~/.cache/breakwater).
#   tools/export.sh [--check]     --check also autoplays every committed log on
#                                 the exported binary and compares its hashes
set -euo pipefail
cd "$(dirname "$0")/.."

version="4.7.2"
base="https://github.com/godotengine/godot/releases/download/${version}-stable"
cache="${BREAKWATER_CACHE:-$HOME/.cache/breakwater}"
templates="${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates/${version}.stable"
tpz="Godot_v${version}-stable_export_templates.tpz"

if [[ ! -f "$templates/linux_release.x86_64" ]]; then
    mkdir -p "$cache"
    [[ -f "$cache/SHA512-SUMS.txt" ]] || curl -sSfL -o "$cache/SHA512-SUMS.txt" "$base/SHA512-SUMS.txt"
    [[ -f "$cache/$tpz" ]] || curl -sSfL -o "$cache/$tpz" "$base/$tpz"
    (cd "$cache" && grep " $tpz\$" SHA512-SUMS.txt | sha512sum -c --quiet -)
    mkdir -p "$templates"
    unzip -q -o -j "$cache/$tpz" 'templates/linux_*' 'templates/version.txt' -d "$templates"
fi

cmake --preset release >/dev/null
cmake --build --preset release >/dev/null
mkdir -p build/export game/.godot
grep -qxF "res://bin/breakwater.gdextension" game/.godot/extension_list.cfg 2>/dev/null ||
    echo "res://bin/breakwater.gdextension" >> game/.godot/extension_list.cfg
"${GODOT_BIN:-godot}" --headless --path game --export-release "Linux" "$PWD/build/export/breakwater.x86_64" \
    >build/export/export.log 2>&1 || { tail -20 build/export/export.log; exit 1; }
echo "export: $(ls build/export/)"

[[ "${1:-}" == "--check" ]] || exit 0
status=0
for log in tests/logs/*.log; do
    expected="$(grep '^end ' "${log%.log}.hashes" | tail -1 | cut -d' ' -f3)"
    out="$(timeout 180 build/export/breakwater.x86_64 --headless -- --autoplay="$PWD/$log" 2>&1)" || true
    if grep -q "hash $expected" <<<"$out"; then
        echo "  ok   exported build autoplays $log: $expected"
    else
        echo "  FAIL exported build $log: expected $expected"; tail -3 <<<"$out"; status=1
    fi
done
exit $status
