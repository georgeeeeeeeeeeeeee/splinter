"""Capability survey: every feature from gentle to extreme, on several kinds of material, measured.
Writes build/survey/survey.csv and prints anything suspicious (non-finite, hot, silent, no effect,
DC, clicks). tools/survey.py [fixtures...]   (default: drums mix pad voice)"""
import csv
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, __import__("os").path.dirname(__file__))
from preset_lab import *  # noqa

FIXTURES = sys.argv[1:] or ["drums", "mix", "pad", "voice"]
W = {"mix": 100}   # fully wet, so each feature is heard on its own
S = []


def add(group, name, p): S.append((group, name, p))


# Clock and window
for st in STEP:
    for r in (1, 4, 16):
        add("clock", f"host {st} rep {r}", P(host(st), W, repeats=r))
for t in (0.0, 0.25, 0.5, 0.75, 0.9, 1.0):
    add("clock", f"internal {t} rep 4", P(internal(t), W, repeats=4))
add("clock", "repeats 32 x8", P(host("x8"), W, repeats=32))
add("clock", "repeats 32 /16", P(host("/16"), W, repeats=32))
for wnd in (0, 50, 100):
    add("window", f"windowing {wnd}", P(host("x2"), W, repeats=4, windowing=wnd))
for wd in (0, 100):
    add("window", f"width {wd}", P(host("x1"), W, repeats=2, width=wd))

# Macro Bend and Break, zone by zone
for z in range(1, 7):
    for pos in (0.2, 0.9):
        add("macro bend", f"zone {z} pos {pos}", P(host("x1"), W, bend(z, pos), repeats=2))
        add("macro break", f"zone {z} pos {pos}", P(host("x1"), W, brk(z, pos), repeats=4))
for st in ("/4", "x4", "x8"):
    add("macro both", f"everything {st}", P(host(st), W, bend(6, 1.0), brk(6, 1.0), repeats=4))

# Micro
for o in (-3, -2, -1, -0.5, 0.5, 1, 2, 3):
    add("micro speed", f"{o:+} oct", P(host("x1"), W, micro(o), repeats=2))
add("micro speed", "reverse", P(host("x1"), W, micro(0, reverse=True), repeats=2))
add("micro speed", "reverse -1 oct", P(host("x1"), W, micro(-1, reverse=True), repeats=2))
for b in (0.2, 0.5, 0.9, 1.0):
    add("micro break", f"silence {b}", P(host("x1"), W, micro(silence=True, brk_amount=b), repeats=4))
    add("micro break", f"traverse {b}", P(host("x1"), W, micro(brk_amount=b), repeats=8))

# Corrupt
for kind in ("Decimate", "Dropout", "Destroy", "Vinyl"):
    for a in (0.1, 0.3, 0.5, 0.7, 0.9, 1.0):
        add("corrupt", f"{kind} {a}", P(host("x1"), W, corrupt(kind, a), repeats=1))
for a in (0.0, 0.2, 0.45, 0.55, 0.8, 1.0):
    add("corrupt", f"DJ Filter {a}", P(host("x1"), W, corrupt("DJ Filter", a), repeats=1))

# Modulation, including the extremes
for shape in ("Sine", "Square", "S&H", "Smooth Random"):
    for dest, depth in (("Time", 30), ("Repeats", 60), ("Bend", 50), ("Corrupt", 80), ("Mix", 100)):
        base = micro() if dest == "Bend" else {}
        extra = corrupt("Decimate", 0.3) if dest == "Corrupt" else {}
        add("mod", f"{shape} 1/8 -> {dest}", P(host("x2"), W, base, extra, mod(1, shape, sync="1/8"), route(1, "Mod 1", dest, depth), repeats=4))
add("mod", "sine 40 Hz -> Time 100", P(internal(0.5), W, mod(1, "Sine", rate=40), route(1, "Mod 1", "Time", 100), repeats=4))
add("mod", "sine 40 Hz -> Bend 100 (micro)", P(host("x1"), W, micro(), mod(1, "Sine", rate=40), route(1, "Mod 1", "Bend", 100), repeats=2))
add("mod", "S&H 1/32 -> Repeats 100", P(host("x4"), W, mod(1, "S&H", sync="1/32"), route(1, "Mod 1", "Repeats", 100), repeats=4))
add("mod", "env -> Break 100", P(host("x1"), W, brk(3, 0.5), env(1, 150, 12), route(1, "Env", "Break", 100), repeats=4))
add("mod", "env -> Corrupt 100 destroy", P(host("x1"), W, corrupt("Destroy", 0.0), env(1, 300, 12), route(1, "Env", "Corrupt", 100), repeats=1))
add("mod", "env -> Mix -100 (ducking)", P(host("x1"), {"mix": 100}, env(5, 400, 6), route(1, "Env", "Mix", -100), repeats=2))
add("mod", "8 routes stacked", P(host("x2"), W, micro(), corrupt("Decimate", 0.2),
    mod(1, "Sine", sync="1/4"), mod(2, "S&H", sync="1/16"), mod(3, "Smooth Random", rate=3), mod(4, "Square", sync="1/2"),
    route(1, "Mod 1", "Bend", 40), route(2, "Mod 2", "Repeats", 60), route(3, "Mod 3", "Time", 30), route(4, "Mod 4", "Break", 50),
    route(5, "Mod 1", "Corrupt", 40), route(6, "Mod 2", "Mix", -30), route(7, "Mod 3", "Bend", -30), route(8, "Mod 4", "Repeats", 30), repeats=4))

# Everything at once, at the limits
add("extreme", "all max", P(host("x8"), W, bend(6, 1.0), brk(6, 1.0), corrupt("Destroy", 1.0), repeats=32, windowing=0, width=100))
add("extreme", "all max internal fastest", P(internal(1.0), W, bend(6, 1.0), brk(6, 1.0), corrupt("Decimate", 1.0), repeats=32, windowing=0, width=100))
add("extreme", "slowest everything", P(internal(0.0), W, bend(6, 1.0), brk(6, 1.0), corrupt("Vinyl", 1.0), repeats=32, windowing=100))
add("extreme", "freeze on", P(host("x1"), W, {"freeze": True}, repeats=4))
add("extreme", "safety clip off, destroy 1 width 100", P(host("x1"), W, corrupt("Destroy", 1.0), {"safetyClip": False}, repeats=1, width=100))
add("extreme", "safety clip off, DJ high-pass 1 width 100", P(host("x1"), W, corrupt("DJ Filter", 1.0), {"safetyClip": False}, repeats=2, width=100))


def job(item):
    group, name, p = item
    row = {"group": group, "name": name}
    for f in FIXTURES:
        try:
            m = measure(render(p, f), f)
        except Exception as e:  # a bad parameter set is itself a finding
            m = {"error": str(e)[:80]}
        row.update({f"{f}.{k}": v for k, v in m.items()})
    return row


rows = list(ThreadPoolExecutor(8).map(job, S))
out = ROOT / "build/survey"
out.mkdir(parents=True, exist_ok=True)
keys = sorted({k for r in rows for k in r}, key=lambda k: (k not in ("group", "name"), k))
with open(out / "survey.csv", "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=keys)
    w.writeheader()
    w.writerows(rows)

print(f"{len(rows)} settings x {len(FIXTURES)} fixtures -> build/survey/survey.csv")
flags = 0
for r in rows:
    for f in FIXTURES:
        g = lambda k: r.get(f"{f}.{k}")
        issues = []
        if g("error"): issues.append("ERROR " + g("error"))
        elif g("finite") is False: issues.append("NON-FINITE")
        else:
            if g("peak") > 0.0: issues.append(f"peak {g('peak')}")
            if g("level") < -15: issues.append(f"quiet {g('level')} dB")
            if g("level") > 8: issues.append(f"loud +{g('level')} dB")
            if g("silent") > 60: issues.append(f"silent {g('silent')}%")
            if g("change") < 1.0: issues.append(f"no audible effect (change {g('change')})")
            if g("dc") > 0.02: issues.append(f"DC {g('dc')}")
            if g("clicks") > 5: issues.append(f"clicks {g('clicks')}/s")
        if issues:
            flags += 1
            print(f"  {r['group']:12} {r['name']:34} {f:6} " + "; ".join(issues))
print(flags, "flags")
