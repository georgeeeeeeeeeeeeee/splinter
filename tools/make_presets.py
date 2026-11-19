"""Writes presets/<Category>/*.json, Splinter's factory presets (compiled into the plug-in).

Ten categories, gentlest first: Repeat, Stutter, Skip, Gate, Tape, Pitch, Lo-Fi, Texture, Ruin, Perform.
Values are each parameter's own units (choices by name). Macro knob positions are given as (zone,
position in zone) and resolved against tuning/macro_tables.json, so re-run this after moving zone
boundaries. Each preset's Output trim comes from tuning/preset_trims.json, which tools/check_presets.py
--calibrate writes, so presets arrive at a matched loudness. Re-run check_presets.py after any change.

Writing an "about": what you hear, then (if it matters) what to do with it. One or two short sentences."""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from preset_lab import bend, brk, corrupt, env, host, internal, micro, mod, route  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "presets"
TRIMS_FILE = ROOT / "tuning" / "preset_trims.json"
CATEGORIES = ["Repeat", "Stutter", "Skip", "Gate", "Tape", "Pitch", "Lo-Fi", "Texture", "Ruin", "Perform"]

P = []
def preset(category, name, about, *parts):
    assert category in CATEGORIES, category
    params = {}
    for p in parts: params.update(p)
    P.append({"name": name, "category": category, "about": about, "params": params})


def semis(n): return n / 12.0
LIVE = internal(1.0)   # a 12.5 ms window at Repeats 1: the wet is the input, 12.5 ms late (for colour presets)
def cc(n, polarity="Unipolar"): return {f"cc{n}Polarity": polarity}


# ---- Repeat: clean, musical, safe on a mix or a send
preset("Repeat", "Quarter Echo", "One clean echo a beat behind, a little wider than the source. Tidy on a bus.",
       host("x1"), {"repeats": 1, "mix": 35, "windowing": 8, "width": 75})
preset("Repeat", "Half-Bar Dub", "Half a bar back, slightly worn and wide: a dub echo for a send.",
       host("/2"), {"repeats": 1, "mix": 32, "windowing": 15, "width": 90}, corrupt("Vinyl", 0.08))
preset("Repeat", "Beat Repeat", "Each beat replays its first half twice; now and then it halves again.",
       host("x1"), {"repeats": 2, "mix": 100, "windowing": 4}, brk(1, 0.4))
preset("Repeat", "Eighth Bounce", "Eighth-note repeats bouncing between the speakers, now and then from elsewhere in the beat.",
       host("x2"), {"repeats": 2, "mix": 60, "windowing": 10, "width": 100}, brk(2, 0.25))
preset("Repeat", "Bar Loop", "The last bar, looped a bar later. Press Freeze to hold a bar while you play over it.",
       host("/4"), {"repeats": 1, "mix": 100, "windowing": 3, "freezeButtonMode": "Latching"})
preset("Repeat", "Fill Maker", "Plays it straight, but every so often a beat turns into a roll. Seed fixed: the same bars fill every time.",
       host("x1"), {"repeats": 1, "mix": 100, "seed": 2026}, brk(3, 0.1))

# ---- Stutter: rolls, ratchets and build-ups
preset("Stutter", "Sixteenth Stutter", "The classic clocked stutter: a new slice count on every sixteenth.",
       host("x4"), {"repeats": 4, "mix": 100}, mod(1, "S&H", sync="1/16"), route(1, "Mod 1", "Repeats", 50))
preset("Stutter", "Ratchet Roll", "Beats break into fast ratchets on the transient.",
       host("x1"), {"repeats": 2, "mix": 100}, brk(3, 0.95))
preset("Stutter", "Triplet Trip", "Triplet ticks with a slice count that changes every triplet eighth.",
       host("x3"), {"repeats": 3, "mix": 100}, mod(1, "S&H", sync="1/8T", polarity="Unipolar"), route(1, "Mod 1", "Repeats", 40), brk(2, 0.3))
preset("Stutter", "Snare Build", "A riser: repeats accelerate across every bar, from one to a blur. Automate Mix to bring it in.",
       host("x1"), {"repeats": 1, "mix": 100, "windowing": 5}, mod(1, "Saw", sync="1 bar", polarity="Unipolar"), route(1, "Mod 1", "Repeats", 100))
preset("Stutter", "Machine Gun", "Audio-rate repeats and drive: a buzz saw on the hits.",
       host("x4"), {"repeats": 8, "mix": 100}, brk(4, 0.4), corrupt("Destroy", 0.35))
preset("Stutter", "Stutter Fade", "Each beat stutters and fades back into the dry signal before the next.",
       host("x1"), {"repeats": 8, "mix": 100, "windowing": 6}, mod(1, "Saw", sync="1/4", polarity="Unipolar"), route(1, "Mod 1", "Mix", -100))

# ---- Skip: a disc, or a buffer, that can't keep its place
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
preset("Skip", "Locked Groove", "A repeatable skip pattern: with the seed fixed, each bar skips the same way every pass.",
       host("x1"), {"repeats": 8, "mix": 100, "seed": 4711}, micro(), mod(1, "S&H", sync="1/16", polarity="Unipolar"), route(1, "Mod 1", "Break", 70))

# ---- Gate: chops and rhythmic holes
preset("Gate", "Gate Chop", "Each slice cut short, more on every other half bar.",
       host("x1"), {"repeats": 4, "mix": 100}, micro(silence=True, brk_amount=0.55), mod(2, "Square", sync="1/2"), route(1, "Mod 2", "Break", 20))
preset("Gate", "Sixteenth Gate", "A tight sixteenth-note gate, half open. Pads and chords turn rhythmic.",
       host("x4"), {"repeats": 1, "mix": 100, "windowing": 6}, micro(silence=True, brk_amount=0.55))
preset("Gate", "Triplet Gate", "A triplet gate: puts a shuffle on straight material.",
       host("x3"), {"repeats": 1, "mix": 100, "windowing": 8}, micro(silence=True, brk_amount=0.65))
preset("Gate", "Half-Time Gate", "Long and short cuts alternating every bar, for a half-time lurch.",
       host("x2"), {"repeats": 2, "mix": 100, "windowing": 5}, micro(silence=True, brk_amount=0.4),
       mod(1, "Square", sync="1 bar"), route(1, "Mod 1", "Break", 35))
preset("Gate", "Random Holes", "Holes punched into the beat at random, some short, some long.",
       host("x2"), {"repeats": 2, "mix": 100}, brk(5, 0.6))
preset("Gate", "Hit Gate", "Hits come through whole; everything between them is chopped away. Play harder for more.",
       host("x4"), {"repeats": 1, "mix": 100, "windowing": 5}, micro(silence=True, brk_amount=0.95),
       env(1, 120, 6), route(1, "Env", "Break", -100))

# ---- Tape: machines slowing, warping and failing
preset("Tape", "Worn Reel", "Now and then the reel drags to a stop; a little wow and dust on top.",
       host("x1"), {"repeats": 1, "mix": 70}, bend(5, 0.3), corrupt("Vinyl", 0.3))
preset("Tape", "Cassette Warble", "A tired cassette: slow pitch drift and soft saturation, a beat behind.",
       host("x1"), {"repeats": 1, "mix": 60, "windowing": 10}, micro(), mod(1, "Smooth Random", rate=0.6), route(1, "Mod 1", "Bend", 3),
       corrupt("Destroy", 0.18))
preset("Tape", "Brake Pedal", "Most half bars, the tape winds down to a standstill and restarts on the next.",
       host("/2"), {"repeats": 1, "mix": 100}, bend(5, 0.9))
preset("Tape", "Power Cut", "Once a bar the deck loses power and grinds to a halt, dusty and dull.",
       host("/4"), {"repeats": 1, "mix": 100}, bend(5, 0.99), corrupt("Vinyl", 0.25))
preset("Tape", "Dropout Deck", "Bad contact: the tape drops out, and sometimes runs backwards.",
       host("x1"), {"repeats": 2, "mix": 100}, bend(1, 0.4), corrupt("Dropout", 0.3))
preset("Tape", "Varispeed Slew", "Pitch glides between speeds every beat, with a touch of surface noise.",
       host("x1"), {"repeats": 1, "mix": 80}, bend(4, 0.6), corrupt("Vinyl", 0.15))

# ---- Pitch: octaves, intervals and direction
preset("Pitch", "Octave Down", "Every beat replays an octave down: heavy, slurred, half speed.",
       host("x1"), {"repeats": 1, "mix": 100, "windowing": 15}, micro(-1.0))
preset("Pitch", "Octave Bloom", "An octave up in soft, wide swells.",
       host("/2"), {"repeats": 2, "mix": 40, "windowing": 100, "width": 90}, micro(1.0))
preset("Pitch", "Fifth Echo", "Each beat echoes a fifth higher: harmony from nothing. Best on melodic parts.",
       host("x1"), {"repeats": 1, "mix": 45, "windowing": 20, "width": 55}, micro(semis(7)))
preset("Pitch", "Backwards", "Every beat plays backwards, a beat late.",
       host("x1"), {"repeats": 1, "mix": 100, "windowing": 6}, micro(0, reverse=True))
preset("Pitch", "Octave Dice", "Random jumps of one and two octaves, up and down, on the beat.",
       host("x1"), {"repeats": 2, "mix": 100}, bend(3, 0.7))
preset("Pitch", "Seasick", "A slow, wide vibrato on everything: the floor is moving.",
       host("x1"), {"repeats": 1, "mix": 100, "windowing": 10}, micro(), mod(1, "Sine", rate=4.5), route(1, "Mod 1", "Bend", 2),
       mod(2, "Smooth Random", rate=0.3), route(2, "Mod 2", "Bend", 2))

# ---- Lo-Fi: colour more than glitch, almost in real time (12.5 ms late)
preset("Lo-Fi", "Early Sampler", "Down to 11 kHz: a dull top and a metallic edge of aliasing, like an early sampler.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Decimate", 0.21))
preset("Lo-Fi", "Old Sampler", "Six bits: crunchy, gritty tails and a hard edge on the hits.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Decimate", 0.45))
preset("Lo-Fi", "Dusty Record", "Crackle, hiss and a narrower top end, with a slow wow.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Vinyl", 0.4))
preset("Lo-Fi", "Old Radio", "Thin, crackly and wobbling, as if from another room.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Vinyl", 0.9))
preset("Lo-Fi", "Filter Drift", "A low-pass that opens and closes over two bars.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("DJ Filter", 0.3), mod(1, "Sine", sync="2 bars"), route(1, "Mod 1", "Corrupt", 22))
preset("Lo-Fi", "Warm Crunch", "Saturation, from warm to crunchy as it gets louder. Level-matched, so it doesn't just get louder.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Destroy", 0.32))

# ---- Texture: clouds, halos and washes (most work best under the dry signal)
preset("Texture", "Frozen Cloud", "Long, soft windows with slow glides. Freeze it to hold a cloud.",
       internal(0.12), {"repeats": 1, "mix": 50, "windowing": 100, "width": 100}, bend(4, 0.2), corrupt("Vinyl", 0.1))
preset("Texture", "Reverse Halo", "The last beat plays backwards behind you, soft-edged and wide.",
       host("x1"), {"repeats": 1, "mix": 45, "windowing": 35, "width": 85}, bend(1, 0.95))
preset("Texture", "Under Water", "Gliding, muffled repeats behind a low-pass.",
       host("x1"), {"repeats": 1, "mix": 60, "windowing": 25, "width": 70}, bend(4, 0.3), corrupt("DJ Filter", 0.22))
preset("Texture", "Dust Choir", "Reversed, wide and dusty swells, a bar at a time.",
       host("/4"), {"repeats": 2, "mix": 50, "windowing": 100, "width": 100}, micro(reverse=True), corrupt("Vinyl", 0.45))
preset("Texture", "Slow Smear", "Long windows that stretch and glide, the length drifting slowly.",
       internal(0.08), {"mix": 55, "windowing": 90}, bend(4, 0.6), mod(1, "Smooth Random", rate=0.1), route(1, "Mod 1", "Time", 6))
preset("Texture", "Half-Speed Drift", "An octave down, soft-edged and a little dusty.",
       host("/2"), {"repeats": 1, "mix": 50, "windowing": 80}, micro(-1.0), corrupt("Vinyl", 0.2))

# ---- Ruin: when it should sound broken
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
preset("Ruin", "Meltdown", "Folded, clipped and pitched about; the louder you play, the further it goes.",
       host("x2"), {"repeats": 2, "mix": 100}, bend(3, 0.6), brk(4, 0.3), corrupt("Destroy", 0.55),
       env(2, 250, 6), route(1, "Env", "Corrupt", 45), route(2, "Env", "Break", 40))

# ---- Perform: for playing. Each one is safe (near dry) until you touch it.
preset("Perform", "Wheel Stutter", "Mod wheel down: dry. Push it up for stutters that get faster and denser.",
       host("x4"), {"repeats": 1, "mix": 0}, cc(1), route(1, "CC 1", "Mix", 100), route(2, "CC 1", "Repeats", 85))
preset("Perform", "Wheel Crush", "The mod wheel crushes it, from clean to a few bits.",
       LIVE, {"repeats": 1, "mix": 100}, corrupt("Decimate", 0.0), cc(1), route(1, "CC 1", "Corrupt", 100))
preset("Perform", "Wheel Wind-Down", "The mod wheel is the motor: pull it up to slow the tape, down to bring it back.",
       LIVE, {"repeats": 1, "mix": 100}, micro(), cc(1), route(1, "CC 1", "Bend", -50))
preset("Perform", "Keyboard Pitch", "Play notes to set the repeats' pitch (C3 is the original). Gate notes C1 to F1 still work.",
       host("x2"), {"repeats": 2, "mix": 100, "noteToSpeed": True, "windowing": 8}, micro())
preset("Perform", "Hold to Glitch", "Dry until you play. Hold C1 to repeat the last beat; add C#1 for tape stops, D1 to break it up.",
       host("x1"), {"repeats": 2, "mix": 0, "gateMode": "Momentary", "freezeButtonMode": "Momentary",
                    "mode": "Macro", "bend": bend(5, 0.8)["bend"], "break": brk(3, 0.8)["break"]})
preset("Perform", "Harder Hits", "Plays straight when you play softly; hit harder and it breaks, stutters and crunches.",
       host("x2"), {"repeats": 2, "mix": 100}, brk(3, 0.0), corrupt("Destroy", 0.0),
       env(1, 180, 4), route(1, "Env", "Break", 55), route(2, "Env", "Corrupt", 40), route(3, "Env", "Repeats", 30))

if __name__ == "__main__":
    trims = json.loads(TRIMS_FILE.read_text()) if TRIMS_FILE.exists() else {}
    names = [p["name"] for p in P]
    assert len(names) == len(set(names)), "duplicate preset names"
    for old in OUT.glob("*/*.json"): old.unlink()
    for d in OUT.iterdir():
        if d.is_dir() and not any(d.iterdir()): d.rmdir()
    count = {}
    for p in P:
        if p["name"] in trims and abs(trims[p["name"]]) >= 0.1:
            p["params"]["output"] = trims[p["name"]]
        d = OUT / p["category"]
        d.mkdir(parents=True, exist_ok=True)
        count[p["category"]] = count.get(p["category"], 0) + 1
        slug = re.sub(r"[^a-z0-9]+", "-", p["name"].lower()).strip("-")
        (d / f"{count[p['category']]:02d}-{slug}.json").write_text(json.dumps(p, indent=1) + "\n")
    print(len(P), "presets:", ", ".join(f"{k} {v}" for k, v in count.items()))
