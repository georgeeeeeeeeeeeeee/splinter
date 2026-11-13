#!/usr/bin/env bash
# Stage 5 renders: every tools/render/stage5/<source>-*.json through build/fixtures/<source>.wav into
# build/renders/stage5 (WAV + PNG + tick log).
# Optional filter: scripts/checkpoint5.sh stutter   (only names containing "stutter")
# Run tools/make_fixtures.py first.
set -euo pipefail
cd "$(dirname "$0")/.."
R=build/tools/render/FaultRender_artefacts/Release/FaultRender
[ -x "$R" ] || R=build/tools/render/FaultRender_artefacts/Release/fault-render
OUT=build/renders/stage5
mkdir -p "$OUT"
for j in tools/render/stage5/*${1:-}*.json; do
  name=$(basename "$j" .json)
  src=build/fixtures/${name%%-*}.wav
  "$R" --in "$src" --out "$OUT/$name.wav" --params "$j" --tables tuning/macro_tables.json --png "$OUT/$name.png" --ticks "$OUT/$name.csv"
done
