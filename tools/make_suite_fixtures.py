"""Writes build/fixtures/suite/: material for auditioning and checking presets beyond the drum loop.
All 120 BPM, 8 bars (16 s), 48 kHz stereo, peaking at -1 dBFS:
  bass.wav  a sixteenth-note synth bassline (filtered saw, plucky envelope)
  pad.wav   a slow chord progression (detuned saws, soft attack, wide)
  keys.wav  a plucked arpeggio (Karplus-Strong), eighth notes, ping-ponged
  mix.wav   drums + bass + keys + pad, roughly mixed: what a bus or master send sees
Run with numpy and scipy: tools/make_suite_fixtures.py (after make_fixtures.py, which makes drums.wav)."""
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, lfilter

SR, BPM, BARS = 48000, 120, 8
BEAT = SR * 60 // BPM
N = BEAT * 4 * BARS
OUT = Path("build/fixtures/suite")
OUT.mkdir(parents=True, exist_ok=True)
rng = np.random.default_rng(7)


def hz(midi): return 440.0 * 2 ** ((midi - 69) / 12)


def saw(f, n, phase=0.0):
    t = np.arange(n) / SR
    return 2 * ((f * t + phase) % 1.0) - 1


def lowpass(x, cutoff):
    b, a = butter(2, min(cutoff, SR * 0.45) / (SR / 2))
    return lfilter(b, a, x)


def norm(st, peak_db=-1.0):
    return st * (10 ** (peak_db / 20) / np.max(np.abs(st)))


def write(name, st):
    wavfile.write(OUT / name, SR, norm(st).astype(np.float32))
    print("wrote", OUT / name)


# Bass: Am - F - C - G roots, sixteenths with a few rests and octave jumps.
roots = [45, 41, 48, 43]
bass = np.zeros(N)
step = BEAT // 4
pattern = [1, 0, 1, 1, 1, 0, 1, 2, 1, 0, 1, 1, 2, 1, 0, 1]   # 0 rest, 2 octave up
for bar in range(BARS):
    root = roots[bar % 4]
    for s, p in enumerate(pattern):
        if p == 0: continue
        at = (bar * 16 + s) * step
        n = int(step * 0.9)
        note = root + (12 if p == 2 else 0)
        t = np.arange(n) / SR
        env = np.exp(-t * 9) * np.minimum(1, t * 400)
        tone = saw(hz(note), n) + 0.5 * saw(hz(note) * 1.005, n)
        cut = 200 + 2500 * np.exp(-t * 25)
        seg = np.zeros(n)
        for i in range(0, n, 256):   # stepped sweep: cheap and good enough
            seg[i:i + 256] = lowpass(tone[max(0, i - 512):i + 256], cut[i])[-len(tone[i:i + 256]):]
        bass[at:at + n] += seg * env
bass_st = np.stack([bass, bass], 1)

# Pad: four chords, a bar each, slow attack and release, detuned and spread.
chords = [[57, 60, 64, 69], [53, 57, 60, 65], [55, 60, 64, 67], [55, 59, 62, 67]]
pad = np.zeros((N, 2))
bar_n = BEAT * 4
for bar in range(BARS):
    at = bar * bar_n
    n = int(bar_n * 1.25)
    t = np.arange(n) / SR
    env = np.minimum(1, t / 0.6) * np.minimum(1, np.maximum(0, (n / SR - t) / 0.5))
    for k, note in enumerate(chords[bar % 4]):
        for side, det in ((0, -0.004), (1, 0.004)):
            v = lowpass(saw(hz(note) * (1 + det), n, phase=rng.random()), 1800) * env / 4
            end = min(N, at + n)
            pad[at:end, side] += v[:end - at]

# Keys: Karplus-Strong plucks, an arpeggio in eighths, alternating left and right.
def pluck(f, n, bright=0.5):
    period = int(SR / f)
    buf = rng.uniform(-1, 1, period)
    out = np.zeros(n)
    for i in range(n):
        v = buf[i % period]
        out[i] = v
        buf[i % period] = 0.996 * (bright * v + (1 - bright) * buf[(i + 1) % period])
    return out

keys = np.zeros((N, 2))
arp = [0, 7, 12, 16, 12, 7, 3, 7]
for bar in range(BARS):
    root = roots[bar % 4] + 12
    for e in range(8):
        at = (bar * 8 + e) * (BEAT // 2)
        n = BEAT
        v = pluck(hz(root + arp[e] + (12 if e == 3 else 0)), n) * np.exp(-np.arange(n) / SR * 3)
        end = min(N, at + n)
        side = e % 2
        keys[at:end, side] += v[:end - at]
        keys[at:end, 1 - side] += 0.35 * v[:end - at]

write("bass.wav", bass_st)
write("pad.wav", pad)
write("keys.wav", keys)

drums_path = Path("build/fixtures/drums.wav")
sr, drums = wavfile.read(drums_path)
drums = drums.astype(np.float64)
if drums.dtype.kind == "i": drums /= 32768.0
if drums.ndim == 1: drums = np.stack([drums, drums], 1)
drums = drums[:N]
if len(drums) < N: drums = np.pad(drums, ((0, N - len(drums)), (0, 0)))
nrm = lambda x: x / np.max(np.abs(x))
mix = 0.9 * nrm(drums) + 0.55 * nrm(bass_st) + 0.35 * nrm(keys) + 0.3 * nrm(pad)
write("mix.wav", mix)
