"""Pitch-tracking check for the tone-chromatic render: each note's output frequency vs 440·2^((n-60)/12).
  check_pitch.py render.wav settings.json"""
import json
import sys

import numpy as np
from scipy.io import wavfile

sr, y = wavfile.read(sys.argv[1])
y = (y[:, 0] if y.ndim > 1 else y).astype(np.float64)
ev = [e for e in json.load(open(sys.argv[2]))["midi"] if e["on"]]
worst, rows = 0.0, []
for e in ev:
    exp = 440.0 * 2 ** ((e["note"] - 60) / 12)
    i0 = int((e["time"] + 0.04) * sr); n = int(0.17 * sr)
    seg = y[i0:i0 + n] * np.hanning(n)
    fs = np.linspace(exp * 0.97, exp * 1.03, 601)
    k = np.arange(n)
    best = max(fs, key=lambda f: abs(np.dot(seg, np.exp(-2j * np.pi * f * k / sr))))
    lo, hi = best - exp * 1e-4, best + exp * 1e-4
    for _ in range(40):
        m1, m2 = lo + (hi - lo) * 0.382, lo + (hi - lo) * 0.618
        p1 = abs(np.dot(seg, np.exp(-2j * np.pi * m1 * k / sr))); p2 = abs(np.dot(seg, np.exp(-2j * np.pi * m2 * k / sr)))
        lo, hi = (lo, m2) if p1 > p2 else (m1, hi)
    c = 1200 * np.log2(0.5 * (lo + hi) / exp)
    worst = max(worst, abs(c))
    rows.append((e["note"], round(c, 3)))
print(json.dumps({"notes": len(rows), "worst_cents": round(float(worst), 3), "within_2_cents": bool(worst < 2.0)}))
