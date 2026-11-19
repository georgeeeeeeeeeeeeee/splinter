"""Preset lab: render presets (or raw parameter sets) through the real plug-in and measure the result.

    from preset_lab import render, measure
    out = render({"params": {...}}, "drums", seed=5)
    m = measure(out, "drums")

Fixtures: drums, voice, tone (build/fixtures) and bass, pad, keys, mix (build/fixtures/suite; run
tools/make_suite_fixtures.py first). Renders use the plug-in's own preset loader (fault-presets
--render), at 120 BPM with the host playing, so a preset behaves here exactly as in a DAW.

Metrics, all against the dry fixture over its own length:
  peak       output peak, dBFS
  level      output RMS relative to the dry, dB
  silent     % of 21 ms frames that are near-silent (< -50 dBFS) where the dry isn't
  change     mean distance between output and dry in 10 log-spaced bands, dB per band per frame:
             0 = identical, ~3 = a light colour, 10+ = transformed
  variety    how much that distance moves over time (std, dB): low = steady effect, high = eventful
  bright     spectral centroid of the output / the dry's (1 = same, < 1 darker)
  width      side / mid RMS of the output (0 = mono)
  clicks     discontinuities a second: sample steps beyond 3x the dry's largest step (and over 0.25)
  density    spectral-flux onsets a second (the dry drum loop is about 6)
"""
import json
import subprocess
import tempfile
from functools import lru_cache
from pathlib import Path

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "build/tools/presets/FaultPresets_artefacts/Release/FaultPresets"
FIX = {"drums": "build/fixtures/drums.wav", "voice": "build/fixtures/voice.wav", "tone": "build/fixtures/tone.wav",
       "bass": "build/fixtures/suite/bass.wav", "pad": "build/fixtures/suite/pad.wav",
       "keys": "build/fixtures/suite/keys.wav", "mix": "build/fixtures/suite/mix.wav"}
SR = 48000
FRAME = 1024


def _read(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 1: x = np.stack([x, x], 1)
    return x


@lru_cache(maxsize=None)
def dry(fixture):
    return _read(ROOT / FIX[fixture])


def render(preset, fixture, seed=5, midi=None, bpm=120, keep=None):
    """preset: a preset dict ({"params": {...}}, name/category optional) or a path. Returns stereo float64."""
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        if isinstance(preset, (str, Path)):
            pfile = Path(preset)
        else:
            pfile = d / "p.json"
            body = {"name": preset.get("name", "lab"), "category": preset.get("category", "Lab"),
                    "about": preset.get("about", ""), "params": preset["params"]}
            pfile.write_text(json.dumps(body))
        out = Path(keep) if keep else d / "o.wav"
        cmd = [str(TOOL), "--render", str(pfile), "--in", str(ROOT / FIX[fixture]), "--out", str(out), "--bpm", str(bpm)]
        if seed is not None: cmd += ["--seed", str(seed)]
        if midi:
            (d / "m.json").write_text(json.dumps(midi))
            cmd += ["--midi", str(d / "m.json")]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0: raise RuntimeError(r.stderr.strip() or r.stdout.strip())
        return _read(out)


def _frames(x):
    n = len(x) // FRAME
    return x[: n * FRAME].reshape(n, FRAME, -1)


_EDGES = np.geomspace(40, 20000, 11)


def _bands(mono):
    fr = _frames(mono[:, None])[:, :, 0] * np.hanning(FRAME)
    spec = np.abs(np.fft.rfft(fr, axis=1)) ** 2
    freqs = np.fft.rfftfreq(FRAME, 1 / SR)
    out = np.stack([spec[:, (freqs >= lo) & (freqs < hi)].sum(1) for lo, hi in zip(_EDGES[:-1], _EDGES[1:])], 1)
    return 10 * np.log10(out + 1e-10), spec, freqs


def _db(v): return 20 * np.log10(max(v, 1e-12))


def measure(out, fixture):
    d = dry(fixture)
    n = len(d)
    o = out[:n]
    if len(o) < n: o = np.pad(o, ((0, n - len(o)), (0, 0)))
    finite = bool(np.all(np.isfinite(out)))
    om, dm = o.mean(1), d.mean(1)
    ob, ospec, freqs = _bands(om)
    db_, dspec, _ = _bands(dm)
    floor = 10 * np.log10((10 ** (-60 / 10)) * FRAME / 2)   # ignore bands quieter than about -60 dB in both
    mask = (ob > floor) | (db_ > floor)
    diff = np.where(mask, np.abs(np.maximum(ob, floor) - np.maximum(db_, floor)), np.nan)
    per_frame = np.nanmean(np.where(mask.any(1, keepdims=True), diff, np.nan), axis=1)
    per_frame = per_frame[np.isfinite(per_frame)]
    o_rms_f = np.sqrt((_frames(o) ** 2).mean((1, 2)))
    d_rms_f = np.sqrt((_frames(d) ** 2).mean((1, 2)))
    live = d_rms_f > 10 ** (-40 / 20)
    silent = float(((o_rms_f < 10 ** (-50 / 20)) & live).sum() / max(1, live.sum()) * 100)
    cen = lambda spec: (spec * freqs).sum(1) / (spec.sum(1) + 1e-12)
    c_o, c_d = cen(ospec), cen(dspec)
    loud = (o_rms_f > 1e-3) & live
    bright = float(np.median(c_o[loud]) / max(1e-9, np.median(c_d[live]))) if loud.any() else 0.0
    mid, side = (o[:, 0] + o[:, 1]) / 2, (o[:, 0] - o[:, 1]) / 2
    width = float(np.sqrt((side ** 2).mean()) / max(1e-12, np.sqrt((mid ** 2).mean())))
    dstep = np.abs(np.diff(d, axis=0)).max()
    # A click: a step bigger than 3x the dry's largest and at least 0.25 (smooth material has tiny steps).
    clicks = float((np.abs(np.diff(o, axis=0)).max(1) > max(3 * dstep, 0.25)).sum() / (n / SR))
    flux = np.maximum(0, np.diff(np.log1p(ospec[:, :200] * 1e3), axis=0)).sum(1)
    thr = np.median(flux) + 2.5 * np.std(flux)
    peaks = np.where((flux[1:-1] > thr) & (flux[1:-1] >= flux[:-2]) & (flux[1:-1] >= flux[2:]))[0]
    both = live[: len(ob)] if len(live) >= len(ob) else np.pad(live, (0, len(ob) - len(live)))
    tilt = [round(float(np.mean(np.maximum(ob[both, k], floor) - np.maximum(db_[both, k], floor))), 1) if both.any() else 0.0 for k in range(10)]
    return {
        "finite": finite,
        "tilt": tilt,
        "peak": round(_db(np.abs(out).max()), 1),
        "level": round(_db(np.sqrt((o ** 2).mean())) - _db(np.sqrt((d ** 2).mean())), 1),
        "silent": round(silent, 1),
        "change": round(float(np.mean(per_frame)), 1) if len(per_frame) else 0.0,
        "variety": round(float(np.std(per_frame)), 1) if len(per_frame) else 0.0,
        "bright": round(bright, 2),
        "width": round(width, 2),
        "clicks": round(clicks, 1),
        "density": round(len(peaks) / (n / SR), 1),
        "dc": round(float(np.abs(o.mean(0)).max()), 4),
    }


def fmt(m):
    return " ".join(f"{k} {v}" for k, v in m.items() if k not in ("finite", "tilt")) + ("" if m["finite"] else "  NON-FINITE")


# ---- Building parameter sets (the same helpers make_presets.py uses, in preset units) ----
TABLES = json.loads((ROOT / "tuning" / "macro_tables.json").read_text())


def _zone(starts, z, pos):
    lo = starts[z]
    hi = starts[z + 1] if z + 1 < len(starts) else 1.0
    return round(lo + pos * (hi - lo), 4)


def bz(z, pos): return _zone(TABLES["bendZoneStart"], z, pos)
def kz(z, pos): return _zone(TABLES["breakZoneStart"], z, pos)


STEP = {"/16": 0.0, "/8": 0.125, "/4": 0.25, "/2": 0.375, "x1": 0.5, "x2": 0.625, "x3": 0.75, "x4": 0.875, "x8": 1.0}
def host(step): return {"clockSource": "Host", "time": STEP[step]}
def internal(t): return {"clockSource": "Internal", "time": t}
def bend(z, pos): return {"mode": "Macro", "bendOnMacro": True, "bend": bz(z, pos)}
def brk(z, pos): return {"mode": "Macro", "breakOnMacro": True, "break": kz(z, pos)}
def corrupt(kind, amount): return {"corruptType": kind, "corrupt": amount}
def mod(n, shape, sync="Free", rate=1.0, polarity="Bipolar", phase=0.0):
    return {f"mod{n}Shape": shape, f"mod{n}Sync": sync, f"mod{n}Rate": rate, f"mod{n}Polarity": polarity, f"mod{n}Phase": phase}
def route(n, source, dest, depth): return {f"route{n}Source": source, f"route{n}Dest": dest, f"route{n}Depth": depth}
def micro(speed_oct=0.0, reverse=False, silence=False, brk_amount=0.0):
    return {"mode": "Micro", "bend": round((speed_oct + 3.0) / 6.0, 4), "bendOnMicro": reverse, "breakOnMicro": silence, "break": brk_amount}
def env(attack=10, release=200, gain=0): return {"envAttack": attack, "envRelease": release, "envGain": gain}


def P(*parts, **kw):
    params = {}
    for p in parts: params.update(p)
    params.update(kw)
    return {"params": params}
