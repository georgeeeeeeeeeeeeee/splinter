# Fault (working title)

A circuit-bent digital buffer effect (AU + VST3). It records audio into a long stereo buffer and, on every clock tick, plays a window of it back broken: stutters, reverse, varispeed, tape stops, skips, dropouts, then bitcrush, saturation, filter and vinyl. An original product.

Status: **Stage 7, release candidate 0.9.0.** Everything from Stages 1–6 (buffer core, Micro and Macro, Corrupt, modulation, editor, 30 presets), hardened: pluginval at strictness 8 and auval pass, state loads from any version, and a 10-minute random-automation fuzz finds no NaN/Inf, overs, stuck gates or audio-thread allocations.

```
build/tests/fault_tests                       # unit tests
python3 tools/make_fixtures.py [voice.wav]    # test material (needs numpy, scipy)
scripts/checkpoint1.sh … checkpoint5.sh   (or .ps1)   # renders: build/renders/stage1..5
scripts/checkpoint6.sh                    (or .ps1)   # every preset through the real plug-in + editor snapshots
scripts/checkpoint7.sh [minutes]          (or .ps1)   # state, fuzz, CPU, pluginval 8, auval
scripts/release.sh                        (or .ps1)   # release candidate zip into dist/
python3 tools/check_pitch.py build/renders/stage2/tone-chromatic.wav tools/render/stage2/tone-chromatic.json
```

## Tuning Macro mode

Every chance, zone boundary and range Macro mode uses is in `tuning/macro_tables.json` (each key is explained in its `_notes`).

- **By render:** edit the file, run `scripts/checkpoint3.sh` (or `.\scripts\checkpoint3.ps1`), and listen to `build/renders/stage3`. The whole grid takes about 4 s. A filter re-renders part of it, e.g. `scripts/checkpoint3.sh break-4`. Each PNG labels what the dice did on every tick (Bend in amber, Break in violet, silence in red), and the `.csv` beside it lists every decision.
- **Live:** dev builds of the plug-in (the default, `FAULT_DEV_TABLES=ON`) watch the file and reload it within half a second of a save. The debug panel shows when it was loaded, or what's wrong with it.
- **Compile it in** when it's right: `fault-render --bake-tables tuning/macro_tables.json` writes `source/dsp/MacroTablesBaked.inc`. Release builds use only that.

The grid's knob positions are given as zone + position, so moving a zone boundary moves the renders with it. Set Seed to anything but 0 for repeatable dice: with the host clock, a given bar then always glitches the same way.

## Corrupt

One effect at a time, picked by Corrupt Type; the Corrupt knob at 0 is off. DJ Filter is the exception: its centre is off, left is low-pass and right is high-pass.

| Type | Knob |
|---|---|
| Decimate | 12 bitcrush / sample-rate presets, one per twelfth of the knob, in a shuffled order from a faint hiss to blown out |
| Dropout | random mutes: few and long on the left (up to a second), many and short on the right (12 ms) |
| Destroy | soft saturation to halfway, then hard clipping and wavefolding; level-matched and limited to −1 dBFS |
| DJ Filter | centre off; left: low-pass 20 kHz → 50 Hz; right: high-pass 20 Hz → 8 kHz |
| Vinyl | crackle, pops, hiss, a narrowing EQ and wow, rising to a hundred-year-old turntable |

Corrupt Set "Original 3" limits it to Decimate, Dropout and Destroy; DJ Filter and Vinyl then play Decimate. Switching type, a Decimate preset step, and turning it on or off are all 30 ms crossfades.

`scripts/checkpoint4.sh` (or `.ps1`) renders the A/B grid into `build/renders/stage4`: `<source>-A-off` is the reference, then every effect at 25/50/75/100% (`drums-vinyl-075`), knob sweeps on drums and on a tone (`tone-destroy-sweep`), a render that switches type every bar, and two with Macro mode on as well.

## Modulation and MIDI

- **Sources:** Mod 1–4 (sine, triangle, saw, square, S&H, smooth random; free 0.01–40 Hz, or synced from 1/32 to 8 bars with dotted and triplet lengths; phase; unipolar or bipolar), Env (an envelope follower on the input), and CC 1–4.
- **Routes:** 8 slots, each a source, a destination (Time, Repeats, Mix, Bend, Break, Corrupt) and a depth from −100% to +100%. Modulation is added to the knob and clamped; it never moves the knob itself, so host automation and modulation stack.
- **Attenuators:** Bend Atten, Break Atten and Corrupt Atten scale all modulation into those three. Bend Atten doesn't apply in Micro mode.
- **CC lanes:** default CC 1 (mod wheel), 16, 17, 18. To learn one, click its "CC# …" button in the mod drawer and move a controller.
- **Gate notes:** Freeze C1, Bend C#1, Break D1, Corrupt D#1, Reset E1, Note clock F1 (Live's numbering, C3 = 60). Each is learnable with its button.
  - Gate Mode Latching: each note-on flips the toggle.
  - Gate Mode Momentary: the toggle is flipped while the note is held.
  - The Bend and Break gates act on the current mode's toggle. The Corrupt gate steps to the next type (Corrupt As Reset makes it a Reset instead).
  - To learn a gate note, click it in the settings drawer and play a note.
- **Clock = Note:** each F1 note-on is a pulse, and Time picks the div/mult. If the pulses stop, the clock carries on at the last rate after four intervals.
- **Restore defaults** (settings drawer) resets the settings the brief lists and clears the routes.

In Live: put Fault on an audio track, make a MIDI track, and set its MIDI To to the Fault track. Drop `tools/live/fault-stage5-gates.mid` onto that MIDI track; it's the same 4 bars as the `drums-gates-clip` render. The settings drawer's "MIDI in" lines show each event's sample offset in its block.

`scripts/checkpoint5.sh` (or `.ps1`) renders the Stage 5 set into `build/renders/stage5`. `tools/make_stage5_settings.py` regenerates the settings and the MIDI clip.

## The editor

- **Left, Buffer:** Time, Repeats, Mix. **Right, Damage:** Bend, Break, Corrupt, each with its on button or type below.
- **Centre, the buffer view:** the active window (left channel above, right below).
  - Slice lines, with the playing slice lighter; it flashes violet when the slice jumps.
  - Each playhead is white going forward and amber in reverse.
  - Silence is hatched red.
  - When Time shrinks, the older audio stays in view as a ghost to the left of the window and fades out.
  - Every tick flashes the window and the lamp in the corner. A blue border means frozen.
- **Knob readouts** speak the knob's language: Time as a note length (Host) or seconds/Hz (Internal), Bend as a zone and position (Macro) or octaves (Micro), Corrupt as the Decimate preset name, the filter frequency, and so on.
  - The lime ring on a knob shows where modulation has taken it.
- **Mod drawer:** modulators with scopes, the envelope follower, CC lanes (learn), and the 8 routes. Drag a source chip onto any of the six knobs to route it (50% depth, or the existing route if there is one).
- **Settings drawer:** windowing, width, stereo, seed, note-to-speed, attenuators, gate and freeze behaviour, Corrupt set and Corrupt-as-reset, Output clip (a safety clipper, on by default: untouched below −1 dBFS, never above 0 dBFS; "Clip" lights in the buffer view when it acts), gate notes (learn), Restore defaults, and a MIDI-in readout.
- **Resizable:** fixed ratio, from 80% to 200% of 900 × 560.

## Presets

30 factory presets in Tape, Skip, Stutter, Ghost, Ruin and Ambient, from the header's browser (< > to step). Save writes your own to the user folder (`~/Library/Application Support/Fault/Presets` on Mac, `%APPDATA%\Fault\Presets` on Windows); they appear under User. A preset lists parameters by ID in their own units, and anything it leaves out goes back to its default. Presets don't touch the CC numbers or gate notes (those belong to the session).

The factory set is generated by `tools/make_presets.py` (Macro knob positions are given as zone and position and read from `tuning/macro_tables.json`, so re-run it after moving zone boundaries). `fault-presets --check` loads each one strictly through the real processor and renders it.

## Hardening and releases

- `fault-harden --state` round-trips every parameter, the CC numbers, the gate notes and the preset name. It loads older, newer and damaged state, and the fixtures in `tests/state/`. When the state format changes, add `tests/state/v<N>.xml` with `--write-fixture`. Never rename a parameter ID.
- `fault-harden --fuzz --minutes 10 [--seed N]` throws random automation, MIDI, transport, block sizes and sample rates at the real processor. It fails on NaN/Inf, a peak over +24 dBFS, a stuck momentary gate, or a heap allocation on the audio thread. It's seeded, so a failure reproduces.
- `fault-harden --cpu` reports processing time against real time for the heaviest settings. `--gain` lists the loudest full-scale cases.
- `scripts/release.sh` builds the release with the tables compiled in and no hot-reload. It checks the tuning file is baked, checks both bundles are universal, ad-hoc signs them, runs pluginval and writes `dist/Fault-<version>-mac.zip`. `scripts/release.ps1` does the same for the Windows VST3. The zip's README is `docs/RELEASE_README.txt`.

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
