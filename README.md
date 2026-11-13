# Fault (working title)

A circuit-bent digital buffer effect (AU + VST3). It records audio into a long stereo buffer and, on every clock tick, plays a window of it back broken: stutters, reverse, varispeed, tape stops, skips, dropouts, then bitcrush, saturation, filter and vinyl. An original product.

Status: **Stage 3**, Macro mode. On top of the buffer core (Stage 1) and Micro mode (Stage 2): per-tick dice for Bend (reverse, octaves, slew, tape stop) and Break (halve, jump, subdivide, audio-rate repeats, silence), Unique / Shared stereo, a seed, and tunable Macro tables. The UI is a debug panel.

```
build/tests/fault_tests                       # unit tests
python3 tools/make_fixtures.py [voice.wav]    # test material (needs numpy, scipy)
scripts/checkpoint1.sh, checkpoint2.sh, checkpoint3.sh   (or .ps1)   # renders: build/renders/stage1..3
python3 tools/check_pitch.py build/renders/stage2/tone-chromatic.wav tools/render/stage2/tone-chromatic.json
```

## Tuning Macro mode

Every chance, zone boundary and range Macro mode uses is in `tuning/macro_tables.json` (each key is explained in its `_notes`).

- **By render:** edit the file, run `scripts/checkpoint3.sh` (or `.\scripts\checkpoint3.ps1`), and listen to `build/renders/stage3`. The whole grid takes about 4 s. A filter re-renders part of it, e.g. `scripts/checkpoint3.sh break-4`. Each PNG labels what the dice did on every tick (Bend in amber, Break in violet, silence in red), and the `.csv` beside it lists every decision.
- **Live:** dev builds of the plug-in (the default, `FAULT_DEV_TABLES=ON`) watch the file and reload it within half a second of a save. The debug panel shows when it was loaded, or what's wrong with it.
- **Compile it in** when it's right: `fault-render --bake-tables tuning/macro_tables.json` writes `source/dsp/MacroTablesBaked.inc`. Release builds use only that.

The grid's knob positions are given as zone + position, so moving a zone boundary moves the renders with it. Set Seed to anything but 0 for repeatable dice: with the host clock, a given bar then always glitches the same way.

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
