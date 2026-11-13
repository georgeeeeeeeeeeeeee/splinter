# Decisions

## D1 — Toolchain and layout (Stage 0)
JUCE 8.0.12 (the checkout at `~/JUCE`), CMake ≥ 3.22, C++20. Mac builds are universal (arm64 + x86_64), deployment target macOS 11; Windows builds use Visual Studio 2022 x64. The JUCE path comes from `-DFAULT_JUCE_PATH`, then `$JUCE_PATH`, then `~/JUCE` (Mac) or `C:\JUCE` (Windows). `source/dsp` is a plain C++ static library with no JUCE dependency.

## D2 — Plugin configuration
AU (Mac only), VST3 and Standalone. An audio effect that accepts MIDI: `NEEDS_MIDI_INPUT`, not a synth, not a MIDI effect. AU main type `kAudioUnitType_MusicEffect` (`aumf`), which hosts require for an effect that takes MIDI. Stereo in and out, mono in to stereo out allowed. Zero latency.

## D3 — Identity is provisional
Company and manufacturer code "Lvrs" (the placeholder George accepted for Refract), plugin code `Flt1`, bundle ID `com.lvrs.fault`, product name "Fault" (working title). All in `cmake/PluginIds.cmake`. They must be fixed before the first release, since hosts store them in saved sets.

## D4 — The reference manual stays out of the repo
`.gitignore` excludes `manuals/` and `*.pdf`. The Data Bender manual is a behavioural reference only: no text, names, artwork or colours from it go into code, UI or docs.

## D5 — Windows scripts are untested so far
`build.ps1`, `install.ps1` and `build-pluginhost.ps1` were written on the Mac, which has no PowerShell. First run on George's Windows machine.
