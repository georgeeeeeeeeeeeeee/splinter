#!/usr/bin/env bash
# Stage 6: every factory preset through the real plug-in processor (drums and voice) into
# build/renders/presets, with a level report, plus editor snapshots in build/renders/ui.
set -euo pipefail
cd "$(dirname "$0")/.."
P=build/tools/presets/FaultPresets_artefacts/Release/FaultPresets
[ -x "$P" ] || P=build/tools/presets/FaultPresets_artefacts/Release/fault-presets
"$P" --check --in build/fixtures/drums.wav --out build/renders/presets/drums
"$P" --check --in build/fixtures/voice.wav --out build/renders/presets/voice
mkdir -p build/renders/ui
"$P" --snapshot build/renders/ui/main.png --in build/fixtures/drums.wav --preset "Gate Chop" --seconds 5.2
"$P" --snapshot build/renders/ui/mod.png --in build/fixtures/drums.wav --preset "Total Fault" --drawer mod
"$P" --snapshot build/renders/ui/settings.png --in build/fixtures/drums.wav --drawer settings
