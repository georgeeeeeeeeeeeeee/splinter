#!/usr/bin/env bash
# Stage 7: hardening. Unit tests, state round trips (incl. earlier versions' fixtures), the
# random-automation fuzz, the CPU benchmark and (if installed) pluginval at strictness 8 + auval.
#   scripts/checkpoint7.sh [fuzz minutes, default 10]
set -euo pipefail
cd "$(dirname "$0")/.."
H=build/tools/harden/FaultHarden_artefacts/Release/FaultHarden
mkdir -p build/stage7
build/tests/fault_tests | tail -1
"$H" --state
"$H" --fuzz --minutes "${1:-10}" --seed "${FUZZ_SEED:-20261114}" | tee build/stage7/fuzz.txt
"$H" --cpu --seconds 10 | tee build/stage7/cpu.txt
PV="$HOME/Applications/pluginval.app/Contents/MacOS/pluginval"
if [ -x "$PV" ]; then
  for b in ~/Library/Audio/Plug-Ins/VST3/Splinter.vst3 ~/Library/Audio/Plug-Ins/Components/Splinter.component; do
    "$PV" --strictness-level 8 --validate-in-process --validate "$b" > "build/stage7/pluginval-$(basename "$b").txt" 2>&1 \
      && echo "pluginval 8: $(basename "$b") passed" || { echo "pluginval 8: $(basename "$b") FAILED (build/stage7/)"; exit 1; }
  done
fi
auval -v aumf Flt1 Lvrs > build/stage7/auval.txt 2>&1 && echo "auval: passed" || { echo "auval FAILED (build/stage7/auval.txt)"; exit 1; }
