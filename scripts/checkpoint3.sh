#!/usr/bin/env bash
# Stage 3 render grid: every tools/render/stage3/<source>-*.json through build/fixtures/<source>.wav,
# with the Macro tables from tuning/macro_tables.json, into build/renders/stage3 (WAV + PNG + tick log).
# Optional filter, for the tuning loop: scripts/checkpoint3.sh bend   (only names containing "bend")
# Run tools/make_fixtures.py first.
set -euo pipefail
cd "$(dirname "$0")/.."
R=build/tools/render/FaultRender_artefacts/Release/FaultRender
[ -x "$R" ] || R=build/tools/render/FaultRender_artefacts/Release/fault-render
OUT=build/renders/stage3
mkdir -p "$OUT"
for j in tools/render/stage3/*${1:-}*.json; do
  name=$(basename "$j" .json)
  src=build/fixtures/${name%%-*}.wav
  "$R" --in "$src" --out "$OUT/$name.wav" --params "$j" --tables tuning/macro_tables.json --png "$OUT/$name.png" --ticks "$OUT/$name.csv"
done
