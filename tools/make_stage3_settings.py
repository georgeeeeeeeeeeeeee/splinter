"""Writes tools/render/stage3/*.json, the Stage 3 render grid (committed, so the PowerShell runner needs
no Python). Each Bend and Break zone at three positions (a = 10%, b = 50%, c = 90% of the way through
it). Knob positions are given as zone + position, so the CLI resolves them against whatever
tuning/macro_tables.json says: retune the zone boundaries and the grid follows."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent / "render" / "stage3"
OUT.mkdir(parents=True, exist_ok=True)
for old in OUT.glob("*.json"):
    old.unlink()

BASE = {"clock": "host", "tempo": {"bpm": 120, "playing": True}, "mix": 1.0, "mode": "macro", "block": "random",
        "seed": 1, "stereo": "unique", "length": 10.0, "tail": 0.5}
BEND = ["off", "reverse", "octave", "2oct", "slew", "tapestop", "everything"]
BREAK = ["off", "halve", "jump", "subdivide", "audiorate", "silence", "everything"]
POS = {"a": 0.1, "b": 0.5, "c": 0.9}


def write(name, d):
    json.dump(d, open(OUT / f"{name}.json", "w"), indent=1)


# Bend: x1 (a tick per beat), Repeats 2. Break: x1, Repeats 4 (16th-note slices at 120 BPM).
for z, label in enumerate(BEND):
    for tag, pos in POS.items():
        write(f"drums-bend-{z}{tag}-{label}", {**BASE, "time": 0.5, "repeats": 2, "bendOnMacro": True, "breakOnMacro": False,
                                               "bendZone": z, "bendZonePos": pos, "break": 0})
for z, label in enumerate(BREAK):
    for tag, pos in POS.items():
        write(f"drums-break-{z}{tag}-{label}", {**BASE, "time": 0.5, "repeats": 4, "bendOnMacro": False, "breakOnMacro": True,
                                                "breakZone": z, "breakZonePos": pos, "bend": 0})

# A few in context: both knobs together, Shared vs Unique, voice, the internal clock.
both = {**BASE, "time": 0.5, "repeats": 4, "bendOnMacro": True, "breakOnMacro": True}
write("drums-both-mid-unique", {**both, "bendZone": 3, "bendZonePos": 0.5, "breakZone": 3, "breakZonePos": 0.5})
write("drums-both-mid-shared", {**both, "stereo": "shared", "bendZone": 3, "bendZonePos": 0.5, "breakZone": 3, "breakZonePos": 0.5})
write("drums-both-everything", {**both, "bendZone": 6, "bendZonePos": 0.7, "breakZone": 6, "breakZonePos": 0.7})
write("voice-both-slew-silence", {**both, "repeats": 2, "length": 14.0, "tail": 1.0,
                                  "bendZone": 4, "bendZonePos": 0.6, "breakZone": 5, "breakZonePos": 0.6})
write("voice-tapestop", {**both, "time": 0.5, "repeats": 1, "breakOnMacro": False, "length": 14.0, "tail": 1.0,
                         "bendZone": 5, "bendZonePos": 0.9})
write("drums-internal-everything", {**both, "clock": "internal", "time": 0.62, "repeats": 3,
                                    "bendZone": 6, "bendZonePos": 0.5, "breakZone": 6, "breakZonePos": 0.5})
print(len(list(OUT.glob("*.json"))), "settings")
