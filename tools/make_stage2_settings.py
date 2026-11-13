"""Writes tools/render/stage2/*.json (committed, so the PowerShell runner needs no Python)."""
import json
import random
from pathlib import Path

OUT = Path(__file__).resolve().parent / "render" / "stage2"
OUT.mkdir(parents=True, exist_ok=True)
HOST = {"clock": "host", "tempo": {"bpm": 120, "playing": True}, "mix": 1.0, "mode": "micro", "block": "random"}
GATES = set(range(36, 42))


def notes(seq, start, dur, gap=0.02):
    ev, t = [], start
    for n in seq:
        ev += [{"time": round(t, 4), "note": n, "on": True}, {"time": round(t + dur - gap, 4), "note": n, "on": False}]
        t += dur
    return ev


def write(name, d):
    json.dump(d, open(OUT / f"{name}.json", "w"), indent=1)


# Every semitone C0..C6 except the gate notes, 0.25 s each, on a steady 440 Hz in an 8 s window.
scale = [n for n in range(24, 97) if n not in GATES]
write("tone-chromatic", {**HOST, "time": 0.0, "repeats": 1, "noteToSpeed": True, "tail": 0, "midi": notes(scale, 8.2, 0.25)})
melody = [60, 63, 67, 70, 72, 70, 67, 63, 55, 58, 60, 62, 63, 62, 60, 55, 48, 60, 72, 84]
write("voice-melody-frozen", {**HOST, "time": 0.25, "repeats": 2, "noteToSpeed": True, "tail": 9,
                              "automation": [{"time": 3.8, "param": "freeze", "value": True}], "midi": notes(melody, 4.2, 0.45)})
write("drums-melody", {**HOST, "time": 0.375, "repeats": 1, "noteToSpeed": True, "midi": notes(melody * 2, 1.0, 0.5)})
write("drums-traverse-sweep", {**HOST, "time": 0.375, "repeats": 16,
                               "automation": [{"time": round(2 + 0.12 * i, 3), "param": "break", "value": round(i / 80, 4)} for i in range(81)]})
random.seed(4)
write("drums-traverse-skips", {**HOST, "time": 0.375, "repeats": 8,
                               "automation": [{"time": round(1.5 + 0.125 * i, 3), "param": "break", "value": round(random.random(), 3)} for i in range(110)]})
write("drums-reverse", {**HOST, "time": 0.5, "repeats": 2, "reverse": True})
write("voice-reverse", {**HOST, "time": 0.5, "repeats": 1, "reverse": True, "tail": 2})
write("drums-bend-glide", {**HOST, "time": 0.5, "repeats": 1,
                           "automation": [{"time": round(2 + 0.05 * i, 3), "param": "bend",
                                           "value": round(0.5 + 0.5 * __import__("math").sin(i / 200 * 2 * 3.14159), 4)} for i in range(200)]})
write("drums-bend-octaves", {**HOST, "time": 0.5, "repeats": 1,
                             "automation": [{"time": 2.0 + 2 * i, "param": "bend", "value": v} for i, v in enumerate([4 / 6, 5 / 6, 1.0, 2 / 6, 1 / 6, 0.5])]})
write("drums-silence-50", {**HOST, "time": 0.5, "repeats": 4, "silence": True, "break": 0.556})
write("drums-silence-90", {**HOST, "time": 0.5, "repeats": 8, "silence": True, "break": 1.0})
print(sorted(p.name for p in OUT.glob("*.json")))
