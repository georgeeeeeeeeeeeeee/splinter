#!/usr/bin/env bash
# Stage 1 checkpoint renders: every tools/render/stage1/<source>-*.json through build/fixtures/<source>.wav,
# into build/renders/stage1 (WAV + PNG + tick log). Run tools/make_fixtures.py first.
set -euo pipefail
cd "$(dirname "$0")/.."
R=build/tools/render/FaultRender_artefacts/Release/FaultRender
[ -n "$R" ] || R=build/tools/render/FaultRender_artefacts/Release/fault-render
[ -x "$R" ] || R=build/tools/render/FaultRender_artefacts/Release/FaultRender
OUT=build/renders/stage1
mkdir -p "$OUT"
for j in tools/render/stage1/*.json; do
  name=$(basename "$j" .json)
  src=build/fixtures/${name%%-*}.wav
  "$R" --in "$src" --out "$OUT/$name.wav" --params "$j" --png "$OUT/$name.png" --ticks "$OUT/$name.csv"
done
