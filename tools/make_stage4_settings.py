"""Writes tools/render/stage4/*.json, the Stage 4 A/B grid (committed, so the PowerShell runner needs no
Python). Every Corrupt effect at 25/50/75/100% on drums and voice, against a Corrupt-off reference (the A),
plus knob sweeps (0 -> 100% -> 0 over 8 s) on drums and on a plain tone, where a click would be obvious,
and a render that switches type every bar.

The wet path is kept clean (Repeats 1 at 1x, Mix 100%), so the only thing changing is Corrupt."""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent / "render" / "stage4"
OUT.mkdir(parents=True, exist_ok=True)
for old in OUT.glob("*.json"):
    old.unlink()

BASE = {"clock": "host", "tempo": {"bpm": 120, "playing": True}, "time": 0.5, "repeats": 1, "mix": 1.0,
        "mode": "macro", "block": "random", "seed": 1, "tail": 0.5}
FX = ["decimate", "dropout", "destroy", "djfilter", "vinyl"]


def write(name, d):
    json.dump(d, open(OUT / f"{name}.json", "w"), indent=1)


for src, length in (("drums", 8.0), ("voice", 14.0)):
    write(f"{src}-A-off", {**BASE, "length": length, "corrupt": 0.0})
    for fx in FX:
        knobs = [0.25, 0.5, 0.75, 1.0] + ([0.0, 0.1] if fx == "djfilter" else [])   # DJ: 50% is its bypass; 0/10% are deep low-pass
        for k in knobs:
            write(f"{src}-{fx}-{int(k * 100):03d}", {**BASE, "length": length, "corruptType": fx, "corrupt": k})

for src in ("drums", "tone"):
    for fx in FX:
        if fx == "djfilter":   # from the centre: down through low-pass, up through high-pass, back to the centre
            ramp = [{"time": 0.5, "param": "corrupt", "value": 0.5, "to": 0.0, "duration": 2.0},
                    {"time": 2.5, "param": "corrupt", "value": 0.0, "to": 1.0, "duration": 4.0},
                    {"time": 6.5, "param": "corrupt", "value": 1.0, "to": 0.5, "duration": 2.0}]
            start = 0.5
        else:
            ramp = [{"time": 0.5, "param": "corrupt", "value": 0.0, "to": 1.0, "duration": 4.0},
                    {"time": 4.5, "param": "corrupt", "value": 1.0, "to": 0.0, "duration": 4.0}]
            start = 0.0
        write(f"{src}-{fx}-sweep", {**BASE, "length": 9.5, "corruptType": fx, "corrupt": start, "automation": ramp})

# A new type every bar (2 s) at 60%, round all five twice.
switch = [{"time": 2.0 * i, "param": "corruptType", "value": FX[i % 5]} for i in range(1, 8)]
write("drums-switch-types", {**BASE, "length": 16.0, "corrupt": 0.6, "corruptType": "decimate", "automation": switch})
# In context: Macro Bend + Break with Destroy and Vinyl on top.
ctx = {**BASE, "repeats": 4, "length": 10.0, "bendOnMacro": True, "breakOnMacro": True, "bendZone": 3, "bendZonePos": 0.5,
       "breakZone": 3, "breakZonePos": 0.5}
write("drums-macro-destroy", {**ctx, "corruptType": "destroy", "corrupt": 0.65})
write("drums-macro-vinyl", {**ctx, "corruptType": "vinyl", "corrupt": 0.8})
print(len(list(OUT.glob("*.json"))), "settings")
