"""Writes test material into build/fixtures: a synthesised 120 BPM drum loop (kick, snare, hats; 8 bars)
and, if available, copies a voice recording.  make_fixtures.py [voice.wav]"""
import shutil
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

SR, BPM, BARS = 48000, 120, 8
out = Path("build/fixtures")
out.mkdir(parents=True, exist_ok=True)
beat = int(SR * 60 / BPM)
n = beat * 4 * BARS
x = np.zeros(n)
rng = np.random.default_rng(3)
t = np.arange(int(0.5 * SR)) / SR


def place(sound, at, gain=1.0):
    end = min(n, at + len(sound))
    x[at:end] += gain * sound[: end - at]


kick = np.sin(2 * np.pi * (50 + 110 * np.exp(-t * 30)) * t) * np.exp(-t * 7)
snare = (0.6 * rng.standard_normal(len(t)) * np.exp(-t * 18) + 0.5 * np.sin(2 * np.pi * 190 * t) * np.exp(-t * 25))
hat_noise = rng.standard_normal(len(t))
hat = np.diff(hat_noise, prepend=0) * np.exp(-t * 60) * 0.35
for b in range(4 * BARS):
    at = b * beat
    if b % 4 in (0, 2) or (b % 8 == 7):
        place(kick, at, 0.9)
    if b % 4 in (1, 3):
        place(snare, at, 0.7)
    for s in range(2):
        place(hat, at + s * beat // 2, 0.8 if s == 0 else 0.5)
    if b % 16 == 15:   # a fill so windows differ
        for s in range(4):
            place(snare, at + s * beat // 4, 0.35 + 0.1 * s)
x = 0.8 * x / np.max(np.abs(x))
wavfile.write(out / "drums.wav", SR, np.stack([x, x], axis=1).astype(np.float32))
if len(sys.argv) > 1 and Path(sys.argv[1]).exists():
    shutil.copy(sys.argv[1], out / "voice.wav")
print("fixtures:", sorted(p.name for p in out.iterdir()))
