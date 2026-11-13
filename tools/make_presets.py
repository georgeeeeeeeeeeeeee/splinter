"""Writes presets/<Category>/*.json, Fault's factory presets (compiled into the plug-in). Values are each
parameter's own units (choices by name). Macro knob positions are given as (zone, position in zone) and
resolved against tuning/macro_tables.json, so re-run this after moving zone boundaries."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
T = json.loads((ROOT / "tuning" / "macro_tables.json").read_text())
OUT = ROOT / "presets"


def zone(starts, z, pos):
    lo = starts[z]
    hi = starts[z + 1] if z + 1 < len(starts) else 1.0
    return round(lo + pos * (hi - lo), 4)


def bz(z, pos): return zone(T["bendZoneStart"], z, pos)
def kz(z, pos): return zone(T["breakZoneStart"], z, pos)


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


P = []
def preset(category, name, about, *parts):
    params = {}
    for p in parts: params.update(p)
    P.append({"name": name, "category": category, "about": about, "params": params})


# ---- Tape
preset("Tape", "Worn Reel", "Now and then the reel drags to a stop; a little wow and dust on top.",
       host("x1"), {"repeats": 1, "mix": 70}, bend(5, 0.3), corrupt("Vinyl", 0.3))
preset("Tape", "Cassette Warble", "A tired cassette: slow pitch drift and soft saturation, a beat behind.",
       host("x1"), {"repeats": 1, "mix": 60, "windowing": 10}, micro(), mod(1, "Smooth Random", rate=0.6), route(1, "Mod 1", "Bend", 3),
       corrupt("Destroy", 0.18))
preset("Tape", "Brake Pedal", "Most bars, the tape winds down to a standstill and restarts on the next.",
       host("/2"), {"repeats": 1, "mix": 100}, bend(5, 0.9))
preset("Tape", "Dropout Deck", "Bad contact: the tape drops out, and sometimes runs backwards.",
       host("x1"), {"repeats": 2, "mix": 100}, bend(1, 0.4), corrupt("Dropout", 0.3))
preset("Tape", "Varispeed Slew", "Pitch glides between speeds every beat, with a touch of surface noise.",
       host("x1"), {"repeats": 1, "mix": 80}, bend(4, 0.6), corrupt("Vinyl", 0.15))

# ---- Skip
preset("Skip", "CD Skip", "A disc that can't find its place: the slice jumps on every eighth.",
       host("x1"), {"repeats": 8, "mix": 100}, micro(), mod(1, "S&H", sync="1/8", polarity="Unipolar"), route(1, "Mod 1", "Break", 100))
preset("Skip", "Scratched Disc", "Jumps to random slices with a grainy edge.",
       host("x2"), {"repeats": 8, "mix": 100}, brk(2, 0.7), corrupt("Decimate", 0.3))
preset("Skip", "Laser Fault", "The laser stutters into small pieces and loses resolution.",
       host("x1"), {"repeats": 4, "mix": 80, "width": 70}, brk(3, 0.5), corrupt("Decimate", 0.4))
preset("Skip", "Anti-Shock Off", "Free-running skips and backwards hiccups, off the beat.",
       internal(0.55), {"repeats": 4, "mix": 100}, brk(2, 0.5), bend(1, 0.3))
preset("Skip", "Track Seek", "Searching for the next track: buzzing seeks through a thin high-pass.",
       host("x1"), {"repeats": 4, "mix": 100}, brk(4, 0.2), corrupt("DJ Filter", 0.7))

# ---- Stutter
preset("Stutter", "Sixteenth Stutter", "The classic clocked stutter: a new slice count on every sixteenth.",
       host("x4"), {"repeats": 4, "mix": 100}, mod(1, "S&H", sync="1/16"), route(1, "Mod 1", "Repeats", 50))
preset("Stutter", "Ratchet Roll", "Beats break into fast ratchets on the transient.",
       host("x1"), {"repeats": 2, "mix": 100}, brk(3, 0.95))
preset("Stutter", "Machine Gun", "Audio-rate repeats and drive: a buzz saw on the hits.",
       host("x4"), {"repeats": 8, "mix": 100}, brk(4, 0.4), corrupt("Destroy", 0.35))
preset("Stutter", "Gate Chop", "Each slice cut short, more on every other half bar.",
       host("x1"), {"repeats": 4, "mix": 100}, micro(silence=True, brk_amount=0.55), mod(2, "Square", sync="1/2"), route(1, "Mod 2", "Break", 20))
preset("Stutter", "Triplet Trip", "Triplet ticks with a slice count that changes every triplet eighth.",
       host("x3"), {"repeats": 3, "mix": 100}, mod(1, "S&H", sync="1/8T", polarity="Unipolar"), route(1, "Mod 1", "Repeats", 40), brk(2, 0.3))

# ---- Ghost
preset("Ghost", "Reverse Halo", "The last beat plays backwards behind you, soft-edged and wide.",
       host("x1"), {"repeats": 1, "mix": 45, "windowing": 35, "width": 85}, bend(1, 0.95))
preset("Ghost", "Octave Ghost", "Octave echoes, up and down, under the dry signal.",
       host("/2"), {"repeats": 1, "mix": 30, "windowing": 45, "width": 80}, bend(3, 0.5))
preset("Ghost", "Faded Memory", "Old audio, worn and half-reversed, half a bar behind.",
       host("/2"), {"repeats": 2, "mix": 50, "windowing": 30}, bend(1, 0.3), corrupt("Vinyl", 0.6))
preset("Ghost", "Under Water", "Gliding, muffled repeats behind a low-pass.",
       host("x1"), {"repeats": 1, "mix": 60, "windowing": 25, "width": 70}, bend(4, 0.3), corrupt("DJ Filter", 0.22))
preset("Ghost", "Whisper Back", "Everything reversed and soft, drifting slightly in pitch.",
       host("/2"), {"repeats": 2, "mix": 40, "windowing": 60}, micro(reverse=True), mod(1, "Triangle", sync="2 bars"), route(1, "Mod 1", "Bend", 2))

# ---- Ruin
preset("Ruin", "Blown Speaker", "Everything at once through a speaker that's had enough.",
       host("x2"), {"repeats": 2, "mix": 100}, bend(6, 0.3), brk(6, 0.3), corrupt("Destroy", 0.85))
preset("Ruin", "Bit Rot", "Crushed to a few bits while the slices fall apart.",
       host("x2"), {"repeats": 4, "mix": 100}, brk(6, 0.5), corrupt("Decimate", 0.95))
preset("Ruin", "Total Fault", "Every dice roll on, slice counts and damage both randomised.",
       host("x4"), {"repeats": 4, "mix": 100}, bend(6, 0.9), brk(6, 0.9), corrupt("Destroy", 0.7),
       mod(1, "S&H", sync="1/16"), route(1, "Mod 1", "Repeats", 40), mod(2, "S&H", sync="1/8", polarity="Unipolar"), route(2, "Mod 2", "Corrupt", 30))
preset("Ruin", "Short Circuit", "Fast free-running windows that turn into tones and crunch.",
       internal(0.82), {"repeats": 3, "mix": 100}, brk(4, 0.6), corrupt("Decimate", 0.7))
preset("Ruin", "Shred", "Torn to pieces: silence and dropouts everywhere.",
       host("x4"), {"repeats": 2, "mix": 100}, brk(5, 0.7), corrupt("Dropout", 0.9))

# ---- Ambient
preset("Ambient", "Frozen Cloud", "Long, soft windows with slow glides. Freeze it to hold a cloud.",
       internal(0.12), {"repeats": 1, "mix": 50, "windowing": 100, "width": 100}, bend(4, 0.2), corrupt("Vinyl", 0.1))
preset("Ambient", "Half-Speed Drift", "An octave down, soft-edged and a little dusty.",
       host("/2"), {"repeats": 1, "mix": 50, "windowing": 80}, micro(-1.0), corrupt("Vinyl", 0.2))
preset("Ambient", "Octave Bloom", "An octave up in soft, wide swells.",
       host("/2"), {"repeats": 2, "mix": 40, "windowing": 100, "width": 90}, micro(1.0))
preset("Ambient", "Slow Smear", "Long windows that stretch and glide, the length drifting slowly.",
       internal(0.08), {"mix": 55, "windowing": 90}, bend(4, 0.6), mod(1, "Smooth Random", rate=0.1), route(1, "Mod 1", "Time", 6))
preset("Ambient", "Dust Choir", "Reversed, wide and dusty swells, a bar at a time.",
       host("/4"), {"repeats": 2, "mix": 50, "windowing": 100, "width": 100}, micro(reverse=True), corrupt("Vinyl", 0.45))

# Write, numbered so the browser keeps this order within a category.
for old in OUT.glob("*/*.json"): old.unlink()
count = {}
for p in P:
    d = OUT / p["category"]
    d.mkdir(parents=True, exist_ok=True)
    count[p["category"]] = count.get(p["category"], 0) + 1
    slug = re.sub(r"[^a-z0-9]+", "-", p["name"].lower()).strip("-")
    (d / f"{count[p['category']]:02d}-{slug}.json").write_text(json.dumps(p, indent=1) + "\n")
print(len(P), "presets:", ", ".join(f"{k} {v}" for k, v in count.items()))
