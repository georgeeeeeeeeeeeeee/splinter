#!/usr/bin/env bash
# Stage 4 A/B grid: every tools/render/stage4/<source>-*.json through build/fixtures/<source>.wav into
# build/renders/stage4 (WAV + PNG + tick log). <source>-A-off is the reference with Corrupt off.
# Optional filter: scripts/checkpoint4.sh vinyl   (only names containing "vinyl")
# Run tools/make_fixtures.py first.
set -euo pipefail
cd "$(dirname "$0")/.."
R=build/tools/render/FaultRender_artefacts/Release/FaultRender
[ -x "$R" ] || R=build/tools/render/FaultRender_artefacts/Release/fault-render
OUT=build/renders/stage4
mkdir -p "$OUT"
for j in tools/render/stage4/*${1:-}*.json; do
  name=$(basename "$j" .json)
  src=build/fixtures/${name%%-*}.wav
  "$R" --in "$src" --out "$OUT/$name.wav" --params "$j" --tables tuning/macro_tables.json --png "$OUT/$name.png" --ticks "$OUT/$name.csv"
done
