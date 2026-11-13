#!/usr/bin/env bash
# Mac: copy AU + VST3 to the user plug-in folders, ad-hoc sign (Apple Silicon won't load unsigned code),
# clear quarantine, refresh the AU cache. Usage: scripts/install.sh [Release|Debug]
set -euo pipefail
cd "$(dirname "$0")/.."
ART="build/Fault_artefacts/${1:-Release}"
install_bundle() {
  local src="$1" dir="$2"
  [ -d "$src" ] || { echo "missing $src: run scripts/build.sh first"; exit 1; }
  mkdir -p "$dir"
  rm -rf "$dir/$(basename "$src")"
  cp -R "$src" "$dir/"
  codesign --force --deep --sign - "$dir/$(basename "$src")" > /dev/null 2>&1
  xattr -dr com.apple.quarantine "$dir/$(basename "$src")" 2>/dev/null || true
  echo "installed $dir/$(basename "$src")"
}
install_bundle "$ART/AU/Fault.component" "$HOME/Library/Audio/Plug-Ins/Components"
install_bundle "$ART/VST3/Fault.vst3" "$HOME/Library/Audio/Plug-Ins/VST3"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "done. Validate the AU with: auval -v aumf Flt1 Lvrs"
