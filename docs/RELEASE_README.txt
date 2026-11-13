FAULT (working title), release candidate
=========================================

A circuit-bent buffer effect. Fault keeps recording the last minute or so of whatever you feed it.
On every clock tick it plays a window of that recording back, broken: stutters, reverse, varispeed,
tape stops, skips and dropouts, then bitcrushing, saturation, filtering and vinyl wear on top.

Formats: AU and VST3 for macOS (Apple Silicon and Intel, macOS 11 or later), VST3 for Windows (64-bit).
These builds aren't signed with a developer certificate, so each system needs one extra step to install.


INSTALL: MAC
------------
1. Copy Fault.component to   ~/Library/Audio/Plug-Ins/Components/
   Copy Fault.vst3      to   ~/Library/Audio/Plug-Ins/VST3/
   (In Finder: Go > Go to Folder..., then paste the path.)
2. Open Terminal and clear the download quarantine:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Fault.component
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fault.vst3
3. In your DAW, rescan plug-ins. In Live: Settings > Plug-Ins, turn on the AU and VST3 system
   folders, then Rescan.

If macOS says the plug-in "is damaged" or "can't be opened", step 2 was missed.

INSTALL: WINDOWS
----------------
1. Copy the Fault.vst3 folder to   C:\Program Files\Common Files\VST3\
2. In your DAW, rescan plug-ins. In Live: Options > Preferences > Plug-Ins, turn on the VST3 system
   folder, then Rescan.

If Windows SmartScreen warns about the file, choose "More info", then "Run anyway" (or unblock it in
the file's Properties).


GETTING STARTED
---------------
Put Fault on an audio track and play something into it. Pick a preset from the browser at the top
(the < and > buttons step through them), or start from Init:

  Time       how long each window is. With Clock = Host it's a note length; Internal is free time.
  Repeats    how many times each window repeats before the next one.
  Mix        dry to wet.
  Bend       the speed and direction of playback (turn it on with the button under the knob).
  Break      cutting the window up: slices, skips and silences.
  Corrupt    one damage effect, chosen under the knob: Decimate, Dropout, Destroy, DJ Filter, Vinyl.
             At 0 it's off. For DJ Filter, the centre is off.

Macro / Micro (under the buffer view):
  Macro   Bend and Break pick what happens from zones on their knobs, with chance deciding what each
          tick does. Set Seed (Settings) to anything but 0 to make the dice repeat exactly.
  Micro   Bend is a direct speed control (in octaves) and Break a direct slice control.

Freeze stops recording so the current buffer is held. Reset restarts the clock.

The buffer view in the middle shows the window being played. White playheads go forward and amber
ones reverse. Slices are marked, silence is hatched, and the border turns blue when frozen.


MODULATION
----------
Click Mod to open the mod drawer, which has four modulators, an envelope follower, four MIDI CC
lanes and 8 routes. Drag a source's chip onto any of the six knobs to route it there. A lime ring
on the knob shows where modulation has moved it.


MIDI
----
Send MIDI to Fault's track to control it with notes. In Live, set a MIDI track's "MIDI To" to the
Fault track.

  C1 Freeze   C#1 Bend   D1 Break   D#1 Corrupt (next type)   E1 Reset   F1 Note clock
  (Live's numbering, where C3 = 60.) Each note is learnable in the Settings drawer.

Gate Mode (Settings): Latching flips on each note. Momentary holds while the note is held.
Other notes can set the playback pitch (Note To Speed, Settings). CC 1, 16, 17 and 18 feed the CC
lanes, and these are learnable too.


PRESETS
-------
There are 30 factory presets in Tape, Skip, Stutter, Ghost, Ruin and Ambient. Save stores your own
in the User section:
  Mac:      ~/Library/Application Support/Fault/Presets
  Windows:  %APPDATA%\Fault\Presets


GOOD TO KNOW
------------
- There's no latency, and the dry signal passes straight through.
- Some combinations are loud. Very resonant DJ Filter settings, Vinyl, and Width at 100% on a hot
  input can all push peaks well above the input level. Keep an eye on your meters, or put a limiter
  after Fault.
- Fault's controls are host-automatable. Saved sessions recall every knob, the learnt CC numbers
  and gate notes, and the preset name.
