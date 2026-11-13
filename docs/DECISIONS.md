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

## D6 — George's Stage 0 answers (13 Nov 2026)
Live Lite is the only test host. Unanswered questions are left at their defaults until George says otherwise:
- name: "Fault" (working title)
- repo: `~/claude/fault` on this Mac
- Cycles: CC lanes stay generic and learnable (no Cycles-specific defaults)
- WASM: kept possible (no JUCE in `/dsp`, plus a C API in `Engine.h`)

## D7 — Engine shape (Stage 1)
`Engine` owns RecordBuffer, Clock, the active Window and two read heads (main + a fade-out tail), and processes sample by sample within a block. Clock ticks are computed per block as exact sample offsets, so output doesn't depend on host block sizes (tested bit-identical at 1, 512 and random sizes). Buffer positions are absolute 64-bit sample counts, so a captured window stays valid while writing continues. The buffer is allocated in `prepare()` for the actual sample rate (65 s), never on the audio thread. The processor prepares the engine at 48 kHz on construction, so it's playable even if a host processes before `prepareToPlay`.

## D8 — Render CLI
`tools/render` (`FaultRender`, JUCE for WAV/JSON/PNG only) drives `dsp::Engine` directly, with a fake host transport. It takes `--key value` arguments and writes the output WAV, a PNG (input in grey behind output, ticks in white, frozen ticks in blue, slice divisions faint) and a tick-log CSV. Checkpoint settings are committed JSON files in `tools/render/stage1/`, run by `scripts/checkpoint1.sh` or `.ps1`. Test material comes from `tools/make_fixtures.py`: a synthesised drum loop, plus a voice recording copied in.

## D9 — Per-mode Bend/Break toggles are four parameters (Stage 2)
The brief's `bendOn` and `breakOn` are "stored per mode". They're implemented as `bendOnMacro`, `bendOnMicro` ("Reverse (Micro)"), `breakOnMacro` and `breakOnMicro` ("Break Silence (Micro)"), so each has its own automation lane and host state. The Stage 6 UI's single Bend and Break buttons will act on the current mode's parameter. Switching mode resets the speed glide to the new mode's own value, so pitch and reverse can't leak across (tested).

## D10 — MIDI reaches the engine sample-accurately
The processor queues each note with its block offset (`Engine::queueNote`), and the engine applies it on that sample. Output with notes is identical at any host block size (tested).
