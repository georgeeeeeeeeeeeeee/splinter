"""Checks the factory preset suite (tools/make_presets.py) against the real plug-in, and calibrates it.

    tools/check_presets.py               check, write build/presets-report.md, exit 1 on a failure
    tools/check_presets.py --calibrate   also set each preset's Output trim (tuning/preset_trims.json)
                                         so the suite plays at a matched loudness; then re-run make_presets.py
    tools/check_presets.py --audition    also write listening renders to build/renders/suite/

Each preset renders on the material its category is for, over three seeds (Macro dice differ per seed),
at 120 BPM; the mix also at 90 and 150 BPM. Perform presets also render with MIDI: a mod-wheel sweep,
held gate notes or a melody, and with none at all (they must be near dry untouched).
Checks: finite; peak at most 0 dBFS; level within 2 dB of dry after the trim; audible (change);
not mostly silent (Gate allows more); no dud seed (a seed that does almost nothing); no near-duplicate in
the same category."""
import json
import math
import statistics as st
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_presets as mp  # noqa: E402
from preset_lab import ROOT, measure, render  # noqa: E402

FOR = {"Repeat": ["drums", "mix", "keys"], "Stutter": ["drums", "mix", "bass"], "Skip": ["drums", "mix", "keys"],
       "Gate": ["pad", "keys", "mix"], "Tape": ["mix", "keys", "drums"], "Pitch": ["keys", "pad", "voice"],
       "Lo-Fi": ["mix", "keys", "voice"], "Texture": ["pad", "keys", "voice"], "Ruin": ["drums", "mix", "bass"],
       "Perform": ["mix", "drums"]}
SEEDS = [1, 2, 3]
MIN_CHANGE = {"Repeat": 2.0, "Lo-Fi": 1.5, "Texture": 2.0}
MAX_SILENT = {"Gate": 80.0}

# Perform: what a player does. Times in seconds (the fixtures are 16 s, the voice 4 s).
def wheel(span=16.0):
    return [{"time": i * span / 64, "cc": 1, "value": int(127 * math.sin(math.pi * i / 64))} for i in range(65)]
def held(note, a, b): return [{"time": a, "note": note, "on": True}, {"time": b, "note": note, "on": False}]
MIDI = {"Wheel Stutter": wheel(), "Wheel Crush": wheel(), "Wheel Wind-Down": wheel(),
        "Keyboard Pitch": [e for i, n in enumerate([60, 67, 55, 72, 48, 64, 60, 53]) for e in held(n, 2 * i, 2 * i + 1.8)],
        "Hold to Glitch": held(36, 2, 5) + held(36, 7, 10) + held(37, 7.2, 10) + held(36, 12, 15) + held(38, 12.2, 15)}
UNTOUCHED_DRY = ["Wheel Stutter", "Wheel Crush", "Wheel Wind-Down", "Hold to Glitch"]


def presets():
    return [p for p in mp.P]


def run(job):
    p, fixture, seed, bpm, midi = job
    m = measure(render(p, fixture, seed=seed, bpm=bpm, midi=midi), fixture)
    return (p["name"], fixture, seed, bpm, midi is not None), m


def main():
    calibrate = "--calibrate" in sys.argv
    audition = "--audition" in sys.argv
    trims = json.loads(mp.TRIMS_FILE.read_text()) if mp.TRIMS_FILE.exists() else {}
    jobs = []
    for p in presets():
        p = json.loads(json.dumps(p))
        if not calibrate and p["name"] in trims and abs(trims[p["name"]]) >= 0.1:
            p["params"]["output"] = trims[p["name"]]
        if calibrate: p["params"].pop("output", None)
        midi = MIDI.get(p["name"])
        for f in FOR[p["category"]]:
            for s in SEEDS:
                jobs.append((p, f, s, 120, midi))
        for bpm in (90, 150):
            jobs.append((p, "mix", 1, bpm, midi))
        if p["name"] in UNTOUCHED_DRY:
            jobs.append((p, "mix", 1, 120, None))
    with ThreadPoolExecutor(8) as ex:
        results = dict(ex.map(run, jobs))

    fails, warns, rows, feats = [], [], [], {}
    new_trims = {}
    for p in presets():
        name, cat = p["name"], p["category"]
        main_ms = [results[(name, f, s, 120, name in MIDI)] for f in FOR[cat] for s in SEEDS]
        tempo_ms = [results[(name, "mix", 1, b, name in MIDI)] for b in (90, 150)]
        allm = main_ms + tempo_ms
        level = st.median(m["level"] for m in main_ms)
        if calibrate:
            raw_peak = max(m["peak"] for m in main_ms + tempo_ms)
            trim = max(-12.0, min(6.0, round(-level * 2) / 2, math.floor((-0.5 - raw_peak) * 2) / 2))   # never into the clipper
            new_trims[name] = trim
            level += trim
        if any(not m["finite"] for m in allm): fails.append(f"{name}: non-finite output")
        peak = max(m["peak"] for m in allm) + (new_trims.get(name, 0) if calibrate else 0)
        if peak > 0.05 and not calibrate: fails.append(f"{name}: peak {peak} dBFS")
        if abs(level) > 2.0 and not calibrate:
            if level < 0 and peak > -1.5 and level > -4.5:   # its boost was capped to stay out of the clipper
                warns.append(f"{name}: {level:+.1f} dB against dry (peak-limited: louder would hit the clipper)")
            else:
                fails.append(f"{name}: level {level:+.1f} dB against dry")
        change = st.median(m["change"] for m in main_ms)
        if change < MIN_CHANGE.get(cat, 3.0): fails.append(f"{name}: barely audible (change {change})")
        silent = max(m["silent"] for m in main_ms)
        if silent > MAX_SILENT.get(cat, 60.0): fails.append(f"{name}: {silent}% silent")
        for f in FOR[cat]:
            ch = [results[(name, f, s, 120, name in MIDI)]["change"] for s in SEEDS]
            if min(ch) < 0.4 * st.median(ch) and st.median(ch) > 2: warns.append(f"{name}: seed-dependent on {f} (change {ch})")
        if name in UNTOUCHED_DRY:
            u = results[(name, "mix", 1, 120, False)]
            if u["change"] > 6: fails.append(f"{name}: not near dry when untouched (change {u['change']})")
        clicks = st.median(m["clicks"] for m in main_ms)
        feats[name] = [st.median(m[k] for m in main_ms) for k in ("change", "variety", "width", "density", "silent")] + \
                      [math.log(max(0.05, st.median(m["bright"] for m in main_ms)))] + \
                      [st.median(m["tilt"][b] for m in main_ms) for b in range(10)]
        rows.append((cat, name, level, peak, change, st.median(m["variety"] for m in main_ms), silent, clicks,
                     new_trims.get(name, trims.get(name, 0.0))))

    # Near-duplicates within a category (features scaled by their spread across the whole suite).
    names = list(feats)
    spread = [st.pstdev(feats[n][i] for n in names) or 1.0 for i in range(len(feats[names[0]]))]
    cat_of = {p["name"]: p["category"] for p in presets()}
    near = []
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if cat_of[a] != cat_of[b]: continue
            d = math.sqrt(sum(((x - y) / s) ** 2 for x, y, s in zip(feats[a], feats[b], spread)))
            if d < 0.6: near.append((d, a, b))
    for d, a, b in sorted(near): warns.append(f"{a} and {b} measure alike (distance {d:.2f})")

    if calibrate:
        mp.TRIMS_FILE.write_text(json.dumps(new_trims, indent=1) + "\n")
        print(f"wrote {mp.TRIMS_FILE} ({len(new_trims)} trims); now run tools/make_presets.py")

    report = ["# Preset suite report", "", f"{len(rows)} presets, {len(jobs)} renders. Level is the median against dry after the Output trim.", "",
              "| Category | Preset | Trim dB | Level dB | Peak dBFS | Change | Variety | Silent % | Clicks/s |", "|---|---|---|---|---|---|---|---|---|"]
    for cat, name, level, peak, change, var, silent, clicks, trim in rows:
        report.append(f"| {cat} | {name} | {trim:+.1f} | {level:+.1f} | {peak:.1f} | {change:.1f} | {var:.1f} | {silent:.0f} | {clicks:.1f} |")
    report += ["", "## Failures", ""] + ([f"- {x}" for x in fails] or ["None."]) + ["", "## Warnings", ""] + ([f"- {x}" for x in warns] or ["None."])
    (ROOT / "build/presets-report.md").write_text("\n".join(report) + "\n")
    print("\n".join(report[4:]) if "--quiet" not in sys.argv else "")
    print(f"\n{len(fails)} failures, {len(warns)} warnings -> build/presets-report.md")

    if audition:
        out = ROOT / "build/renders/suite"
        def aud(p):
            p = json.loads(json.dumps(p))
            if p["name"] in trims: p["params"]["output"] = trims[p["name"]]
            d = out / p["category"]
            d.mkdir(parents=True, exist_ok=True)
            for f in FOR[p["category"]][:2]:
                render(p, f, seed=1, midi=MIDI.get(p["name"]), keep=d / f"{p['name']} - {f}.wav")
        with ThreadPoolExecutor(8) as ex: list(ex.map(aud, presets()))
        print("audition renders in", out)
    sys.exit(1 if fails and not calibrate else 0)


if __name__ == "__main__":
    main()
