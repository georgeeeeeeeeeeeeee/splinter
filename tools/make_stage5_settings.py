"""Writes tools/render/stage5/*.json (the Stage 5 renders, committed so the PowerShell runner needs no
Python) and tools/live/fault-stage5-gates.mid, a 4-bar MIDI clip for Live with the same gates and CCs as
the drums-gates-clip render: drop it on a MIDI track whose MIDI To is the track holding Fault.

Gate notes are Live's numbering (C3 = 60): Freeze C1, Bend C#1, Break D1, Corrupt D#1, Reset E1, Note clock F1."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "render" / "stage5"
OUT.mkdir(parents=True, exist_ok=True)
for old in OUT.glob("*.json"):
    old.unlink()
BPM = 120.0
BEAT = 60.0 / BPM
GATE = {"freeze": 36, "bend": 37, "break": 38, "corrupt": 39, "reset": 40, "clock": 41}
BASE = {"clock": "host", "tempo": {"bpm": BPM, "playing": True}, "mix": 1.0, "mode": "macro", "block": "random", "seed": 3, "tail": 0.5}


def write(name, d):
    json.dump(d, open(OUT / f"{name}.json", "w"), indent=1)


def beats(b):
    return b * BEAT


# ---- The gate clip: 4 bars (16 beats). Latching gates (the default): each note-on flips.
clip = []   # (beat, kind, number, value, on)
def gate(name, at, length=0.25):
    clip.append((at, "note", GATE[name], 100, True))
    clip.append((at + length, "note", GATE[name], 0, False))
gate("freeze", 2.0); gate("freeze", 4.0)            # bar 1 beat 3: freeze; bar 2 beat 1: release
gate("bend", 4.0); gate("bend", 6.0)                # bar 2: Bend on (Macro dice) for two beats
gate("break", 8.0); gate("break", 12.0)             # bar 3: Break on for a bar
gate("reset", 9.5); gate("reset", 14.5)             # off-beat resets
gate("corrupt", 12.0); gate("corrupt", 14.0)        # step the Corrupt type twice
for i in range(64):                                 # CC 1 (lane 1): a ramp over the 4 bars, every 1/16
    clip.append((i * 0.25, "cc", 1, int(round(127 * i / 63)), True))
for i in range(16):                                 # CC 16 (lane 2): high / low every beat
    clip.append((float(i), "cc", 16, 127 if i % 2 == 0 else 0, True))
clip.sort(key=lambda e: (e[0], e[1] != "cc", e[4]))

midi_events = []
for at, kind, num, val, on in clip:
    if kind == "cc":
        midi_events.append({"time": beats(at), "cc": num, "value": val})
    else:
        midi_events.append({"time": beats(at), "note": num, "on": on, "velocity": val if on else 0})
gates_render = {**BASE, "time": 0.625, "repeats": 2, "length": 8.0, "bendOnMacro": False, "breakOnMacro": False,
                "bendZone": 4, "bendZonePos": 0.6, "breakZone": 3, "breakZonePos": 0.7,
                "corrupt": 0.3, "corruptType": "decimate",
                "routes": [{"source": "cc1", "dest": "corrupt", "depth": 0.6}, {"source": "cc2", "dest": "mix", "depth": -0.3}],
                "midi": midi_events}
write("drums-gates-clip", gates_render)

# ---- Synced S&H on Repeats: the classic clocked stutter (x4 = a tick every 1/16, S&H at 1/16).
sh = {**BASE, "time": 0.875, "repeats": 4, "length": 8.0, "mods": [{"shape": "sh", "sync": "1/16", "polarity": "bi"}],
      "routes": [{"source": "mod1", "dest": "repeats", "depth": 0.5}]}
write("drums-sh-repeats-stutter", sh)
write("drums-sh-repeats-8th", {**sh, "time": 0.625, "mods": [{"shape": "sh", "sync": "1/8", "polarity": "uni"}]})
write("voice-sh-repeats-stutter", {**sh, "length": 14.0})

# ---- LFOs and the envelope follower.
write("voice-lfo-bend-micro", {**BASE, "mode": "micro", "time": 0.5, "repeats": 1, "length": 14.0, "bend": 0.5,
                               "mods": [{"shape": "triangle", "sync": "2 bars"}], "routes": [{"source": "mod1", "dest": "bend", "depth": 0.17}]})
write("drums-smooth-time", {**BASE, "clock": "internal", "time": 0.6, "repeats": 3, "length": 10.0,
                            "mods": [{"shape": "smooth", "rate": 0.4}], "routes": [{"source": "mod1", "dest": "time", "depth": 0.25}]})
write("voice-env-destroy", {**BASE, "time": 0.5, "repeats": 1, "length": 14.0, "corruptType": "destroy", "corrupt": 0.2,
                            "env": {"attack": 5, "release": 300, "gain": 6},
                            "routes": [{"source": "env", "dest": "corrupt", "depth": 0.8}]})
write("drums-cc-djfilter", {**BASE, "time": 0.5, "repeats": 1, "length": 8.0, "corruptType": "djfilter", "corrupt": 0.5,
                            "cc": [{"number": 1, "polarity": "bi"}], "routes": [{"source": "cc1", "dest": "corrupt", "depth": 0.5}],
                            "midi": [{"time": 0.5 + 0.01 * i, "cc": 1, "value": int(round(63.5 + 63.5 * __import__("math").sin(i / 60.0 * 6.283)))}
                                     for i in range(700)]})

# ---- Note clock: a swung pulse (long-short), x2, then the pulses stop and it carries on.
pulses = []
t = 0.25
for k in range(12):
    pulses += [{"time": t, "gate": "clock", "on": True}, {"time": t + 0.05, "gate": "clock", "on": False}]
    t += 0.36 if k % 2 == 0 else 0.24
write("drums-noteclock-swing", {**BASE, "clock": "note", "time": 0.625, "repeats": 2, "length": 10.0, "midi": pulses})

# ---- Reset gates on the internal clock, and Corrupt-as-reset.
resets = [{"time": x, "gate": "reset", "on": True} for x in (1.3, 2.05, 3.9, 4.25, 6.6)]
write("drums-reset-gates", {**BASE, "clock": "internal", "time": 0.3, "repeats": 2, "length": 8.0, "midi": resets})
write("drums-corrupt-as-reset", {**BASE, "clock": "internal", "time": 0.3, "repeats": 2, "length": 8.0, "corruptAsReset": True,
                                 "midi": [{**r, "gate": "corrupt"} for r in resets]})

# ---- The MIDI file (SMF type 0, 480 ticks a beat, 120 BPM).
TPB = 480
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)
track = bytearray()
track += vlq(0) + b"\xff\x51\x03" + int(60_000_000 / BPM).to_bytes(3, "big")   # tempo
track += vlq(0) + b"\xff\x03" + bytes([len(b"Fault gates")]) + b"Fault gates"
last = 0
for at, kind, num, val, on in clip:
    tick = int(round(at * TPB))
    track += vlq(tick - last)
    last = tick
    if kind == "cc":
        track += bytes([0xB0, num, val])
    else:
        track += bytes([0x90 if on else 0x80, num, val if on else 0])
end = 16 * TPB
track += vlq(end - last) + b"\xff\x2f\x00"
smf = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPB) + b"MTrk" + struct.pack(">I", len(track)) + bytes(track)
(ROOT / "live").mkdir(exist_ok=True)
(ROOT / "live" / "fault-stage5-gates.mid").write_bytes(smf)
print(len(list(OUT.glob("*.json"))), "settings, plus tools/live/fault-stage5-gates.mid")
