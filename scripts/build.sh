#!/usr/bin/env bash
# Mac: universal (arm64 + x86_64) build of AU, VST3 and Standalone. Usage: scripts/build.sh [Release|Debug]
set -euo pipefail
cd "$(dirname "$0")/.."
CONFIG="${1:-Release}"
CMAKE="${CMAKE:-$(command -v /opt/homebrew/bin/cmake || command -v cmake)}"
GEN=()
command -v ninja > /dev/null && GEN=(-G Ninja)
"$CMAKE" -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE="$CONFIG" > /dev/null
"$CMAKE" --build build --config "$CONFIG"
ART="build/Splinter_artefacts/$CONFIG"
for b in "$ART/AU/Splinter.component" "$ART/VST3/Splinter.vst3" "$ART/Standalone/Splinter.app"; do
  [ -d "$b" ] || continue
  bin="$(find "$b/Contents/MacOS" -type f -maxdepth 1 | head -1)"
  echo "$(basename "$b"): $(lipo -archs "$bin")"
done
