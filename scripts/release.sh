#!/usr/bin/env bash
# Mac release candidate: a universal Release build with the Macro tables compiled in (no hot-reload),
# ad-hoc signed, zipped with the user README. Output: dist/Fault-<version>-mac.zip
# Usage: scripts/release.sh            (pluginval on the VST3 too, if ~/Applications/pluginval.app exists)
set -euo pipefail
cd "$(dirname "$0")/.."
CMAKE="${CMAKE:-$(command -v /opt/homebrew/bin/cmake || command -v cmake)}"
VERSION="$(sed -n 's/^project(Fault VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
B=build-release
GEN=()
command -v ninja > /dev/null && GEN=(-G Ninja)
"$CMAKE" -S . -B "$B" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DFAULT_DEV_TABLES=OFF -DFAULT_BUILD_TESTS=OFF > /dev/null
"$CMAKE" --build "$B" --config Release --target Fault_AU Fault_VST3 FaultRender

# The tuning file must already be baked in: a release never reads tuning/macro_tables.json.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$B/tools/render/FaultRender_artefacts/Release/FaultRender" --bake-tables tuning/macro_tables.json --bake-out "$TMP/baked.inc" > /dev/null
if ! diff -q "$TMP/baked.inc" source/dsp/MacroTablesBaked.inc > /dev/null; then
  echo "tuning/macro_tables.json has changes that aren't baked in. Run:"
  echo "  fault-render --bake-tables tuning/macro_tables.json   (then this script again)"
  exit 1
fi

ART="$B/Fault_artefacts/Release"
OUT="dist/Fault-$VERSION-mac"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT"
for b in AU/Fault.component VST3/Fault.vst3; do
  cp -R "$ART/$b" "$OUT/"
  bundle="$OUT/$(basename "$b")"
  bin="$(find "$bundle/Contents/MacOS" -type f -maxdepth 1 | head -1)"
  archs="$(lipo -archs "$bin")"
  [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || { echo "$bundle is not universal ($archs)"; exit 1; }
  if strings "$bin" | grep -q "macro_tables.json"; then echo "$bundle still watches the tuning file"; exit 1; fi
  codesign --force --deep --sign - "$bundle"
  echo "$(basename "$bundle"): $archs, ad-hoc signed"
done
cp docs/RELEASE_README.txt "$OUT/README.txt"
PV="$HOME/Applications/pluginval.app/Contents/MacOS/pluginval"
if [ -x "$PV" ]; then
  "$PV" --strictness-level 8 --validate-in-process --validate "$OUT/Fault.vst3" > "$TMP/pluginval.txt" 2>&1 \
    && echo "pluginval (strictness 8): VST3 passed" || { tail -30 "$TMP/pluginval.txt"; echo "pluginval failed"; exit 1; }
fi
(cd dist && ditto -c -k --keepParent "Fault-$VERSION-mac" "Fault-$VERSION-mac.zip")
echo "wrote dist/Fault-$VERSION-mac.zip"
