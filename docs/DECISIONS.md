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

## D11 — Two voices (Stage 3)
The engine has one voice per channel (window, head, fade tail, Macro state), each reading its own channel of the buffer. Micro mode and Shared stereo give both voices the same decisions. Stage 1 and 2 output is unchanged (their tests are bit-identical).

## D12 — MacroTables: one struct, a JSON file, a baked include
`dsp/MacroTables.h` holds every Macro number as floats or float arrays, with a field list (name, offset, size, note) that the JSON reader and writer and the baker iterate over. `tuning/macro_tables.json` is the working copy. Dev plug-in builds poll it (every 400 ms, message thread) and hand new tables to the engine through a one-slot lock-free mailbox (`Engine::setTables`); the engine copies them in at the start of the next block. `fault-render --bake-tables` writes `MacroTablesBaked.inc`, which `MacroTables::defaults()` includes. The CMake option `FAULT_DEV_TABLES` (on for now) controls the hot reload and is switched off for release.

## D13 — Randomness
PCG32, one stream per decision kind and channel (Bend L/R, Break L/R). Seed 0: the streams are seeded once per session (the plug-in uses the system RNG; the CLI's `sessionSeed` defaults to 1, so renders are reproducible). Seed ≠ 0: every tick seeds a fresh generator from (seed, stream, tick index). The clock reports each tick's index (`Clock::tickIndex`). Tested: bit-identical at block sizes 1, 512 and random, and identical decisions per grid position from different start points.

## D14 — New parameters (Stage 3)
`stereoMode` (Unique / Shared, default Unique) and `seed` (0–99999, 0 shown as "Random"). State version 4.

## D15 — Corrupt: one stage, crossfaded slots (Stage 4)
`dsp/Corrupt/` holds each effect as a small header-only class (Decimate, Dropout, Destroy, DjFilter, Vinyl; a shared TPT `Svf.h`). `Corrupt` owns one of each (two Decimates) and plays a "slot": an effect, a Decimate preset, or bypass. Any slot change crossfades over 30 ms, with the outgoing slot still running underneath. A change that arrives mid-fade waits for the fade to end. The knob is smoothed over 20 ms for the continuous effects; Decimate's presets are meant to step. Bypassed and not fading, it returns at once. Each effect restarts clean when it comes in. Tested: no step during a 0 → 100% → 0 sweep, or while switching type every 125 ms, is larger than the held effect makes on its own. With the crossfades removed, both tests fail.

## D16 — Vinyl's wow is a delay on the wet, not a playhead speed change
Wow as a modulated delay (at most ~3 ms, linear interpolation) keeps Vinyl self-contained at the end of the chain, independent of the playheads, tape stops and the "exact octave" flag. It isn't reported as latency: the dry path is untouched, and the wet is already a delayed replay of the buffer.

## D17 — New parameters (Stage 4)
`corrupt` (0–1, default 0 = off), `corruptType` (Decimate, Dropout, Destroy, DJ Filter, Vinyl) and `corruptSet` (All / Original 3). State version 5. `corruptAtten` arrives with the mod matrix in Stage 5.

## D18 — Segments and a control grid (Stage 5)
`Engine::process` runs in segments, split at the 32-sample control grid (absolute time) and at every MIDI event. At each grid point the modulators advance and the offsets are recomputed; at each event the gates act. The "effective" parameters (knobs + gates + modulation) are rebuilt at both. Each segment is one clock block. The host's block boundaries change nothing, which keeps the Stage 1–4 bit-identity tests passing unchanged and extends them to modulation, CCs and gates (tested at 1, 512 and random block sizes). `EngineParams` from the host are kept as `base_`; what plays is `p_`, and modulation is never written back.

## D19 — Modulation and gate parameters (Stage 5)
Parameters: `bendAtten`, `breakAtten`, `corruptAtten` (default 100%); `gateMode`; `corruptAsReset`; per modulator `mod<n>Shape/Rate/Sync/Phase/Polarity` (Sync is "Free" or a synced length, so one choice replaces a sync toggle plus a division); `envAttack/Release/Gain`; `cc<n>Polarity`; per route `route<n>Source/Dest/Depth`. 78 parameters in all; state version 6. The CC numbers and gate notes are plug-in state (`ccNumbers` and `gateNotes` properties), learnt from the editor via atomics the audio thread reads each block.

## D20 — The editor (Stage 6)
`source/ui/`: `Theme.h` (colour tokens and the LookAndFeel), `Knob.h` (rotary with a modulation ring; a drag-and-drop target), `Controls.h` (labelled slider, combo, toggle button, source chip, scope), `BufferView`, `ModDrawer`, `SettingsDrawer`. The editor lays everything out once at 900 × 560 inside a content component and scales it with an affine transform, so every size looks the same. It reads the engine only through `Engine::Status` atomics and `Engine::overview()`, a lock-free min/max overview (32-sample buckets, written alongside the record buffer). Everything refreshes from one 60 Hz timer.

## D21 — Presets as JSON, compiled in (Stage 6)
Factory presets are `presets/<Category>/NN-name.json`, compiled in with `juce_add_binary_data`. User presets use the same format in the user's app-data folder. Loading sets every parameter: the listed value, or the default. The plug-in reports a bad key but carries on; `fault-presets --check` is strict and fails. Programs aren't exposed to the host (`getNumPrograms` stays 1): some hosts call `setCurrentProgram (0)` on load, which would overwrite a restored session.

## D22 — `fault-presets`: the real processor offline
A console tool that links the plug-in code. It renders each preset through `FaultProcessor::processBlock` with a fake playhead (120 BPM, playing) and reports peak and RMS against dry, failing on NaN/Inf or a peak over +6 dBFS. It also snapshots the editor to PNG (`--snapshot`, optionally with a drawer open), which is how the UI was checked without a display.
