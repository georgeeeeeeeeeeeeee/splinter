# Fault (working title)

A circuit-bent digital buffer effect (AU + VST3). It records audio into a long stereo buffer and, on every clock tick, plays a window of it back broken: stutters, reverse, varispeed, tape stops, skips, dropouts, then bitcrush, saturation, filter and vinyl. An original product.

Status: **Stage 1**, buffer core. The record buffer, Internal and Host clocks, windows and Repeats, the playhead, slice windowing, Freeze, Mix and Width all work, with a render CLI and unit tests. The UI is a debug panel; MIDI is only logged so far.

```
build/tests/fault_tests                       # unit tests
python3 tools/make_fixtures.py [voice.wav]    # test material (needs numpy, scipy)
scripts/checkpoint1.sh   (or .ps1)             # renders: build/renders/stage1/*.wav + .png + .csv
```

## Install (unsigned build)

Fault isn't signed with an Apple Developer ID, so macOS needs one extra step.

**Mac**
1. Copy `Fault.component` to `~/Library/Audio/Plug-Ins/Components/` and `Fault.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (in Finder: Go → Go to Folder…).
2. In Terminal, clear the download quarantine:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Fault.component
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fault.vst3
   ```
3. In Live: Settings → Plug-Ins → turn on the AU and VST3 system folders → Rescan.

**Windows**
1. Copy `Fault.vst3` to `C:\Program Files\Common Files\VST3\`.
2. In Live: Options → Preferences → Plug-Ins → VST3 system folder on → Rescan.

## Build

Mac (universal binary; CMake, Ninja optional, JUCE 8 at `~/JUCE` or `-DFAULT_JUCE_PATH=`):
```
scripts/build.sh            # AU + VST3 + Standalone
scripts/install.sh          # copy to ~/Library/Audio/Plug-Ins, ad-hoc sign, clear quarantine
auval -v aumf Flt1 Lvrs
```

Windows (PowerShell; Visual Studio 2022, CMake, JUCE 8 at `C:\JUCE` or `-Juce`):
```
.\scripts\build.ps1               # VST3 + Standalone
.\scripts\install.ps1             # as Administrator: copy to Common Files\VST3
.\scripts\build-pluginhost.ps1    # JUCE's AudioPluginHost, for testing without Live
```

Docs: [DECISIONS.md](docs/DECISIONS.md) (architecture choices), [SPEC_NOTES.md](docs/SPEC_NOTES.md) (behaviour inferred beyond the reference manual).
