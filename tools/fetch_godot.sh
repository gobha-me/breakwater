#!/usr/bin/env bash
# Fetches the official Godot 4.7.2 Linux binary once, verifies it against the
# release's SHA512-SUMS.txt, caches it, and prints its path. Used by CI to run
# the Godot gates on an official build rather than a distribution package.
#   GODOT_BIN="$(tools/fetch_godot.sh)" tools/godot_smoke.sh
set -euo pipefail

version="4.7.2"
base="https://github.com/godotengine/godot/releases/download/${version}-stable"
cache="${BREAKWATER_CACHE:-$HOME/.cache/breakwater}"
zip="Godot_v${version}-stable_linux.x86_64.zip"
bin="$cache/Godot_v${version}-stable_linux.x86_64"

if [[ ! -x "$bin" ]]; then
    mkdir -p "$cache"
    [[ -f "$cache/SHA512-SUMS.txt" ]] || curl -sSfL -o "$cache/SHA512-SUMS.txt" "$base/SHA512-SUMS.txt"
    [[ -f "$cache/$zip" ]] || curl -sSfL -o "$cache/$zip" "$base/$zip"
    (cd "$cache" && grep " $zip\$" SHA512-SUMS.txt | sha512sum -c --quiet -)
    unzip -q -o "$cache/$zip" -d "$cache"
fi
echo "$bin"
