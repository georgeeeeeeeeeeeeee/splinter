# Spec notes

Behaviour inferred beyond the reference manual, with reasoning.

- **N1. Internal clock curve.** Period = 16 s × (1/1280)^Time, so Time 0 is 16 s and Time 1 is 1/80 s (12.5 ms), exponential between. Equal knob travel gives equal ratios, which is how a rate knob feels.
- **N2. Host-sync steps.** Time is split into 9 equal zones, slowest first: /16, /8, /4, /2, ×1, ×2, ×3, ×4, ×8 (ticks per beat 1/16 … 8), so the knob's direction matches Internal (clockwise = faster). /16 = one tick every 16 beats. Ticks sit on the host's beat grid (tick k at beat k / rate), so they're phase-locked to bar position, and a transport start or loop jump lands straight back on the grid. With the transport stopped, the clock free-runs at the host tempo and the same step.
- **N3. Window length.** The most recent `period` samples, rounded to a whole sample (capped at what the buffer holds), captured *before* the tick's own sample is recorded. So at Repeats 1 and speed 1 the output is the input exactly one period late, bit for bit.
- **N4. Minimum fade.** Whenever windowing is above 0, a slice-edge fade is at least 1 ms (or half the slice, if that's shorter). On short slices, 2% of a slice is a few samples, which is itself a click. At 0 the edges stay hard.
- **N5. Mid-slice jumps.** When a tick (or Reset) arrives while the current slice is still audible, the old read head fades out over the new slice's fade length (32 samples to 20 ms) instead of being cut. At a slice end its envelope is already down, so nothing is needed.
- **N6. Repeats changes** take effect at the next slice boundary or tick, never mid-slice.
- **N7. Width.** 0–100% maps the wet signal's side to ×0 … ×2. 50% is as recorded (the default), 0 is mono, 100% doubles the side.
- **N8. Freeze modes.** Latching (the default) engages and releases on the next clock tick; Momentary acts at once. Freezing at Mix 0 forces fully wet until release (§3.1).
- **N9. Micro Bend.** Speed = 2^(−3 + 6·Bend) octaves: the knob's centre is 1×, the ends ±3 octaves, smoothed over 20 ms in octaves. "On an exact octave" means within ±5 cents.
- **N10. Note to Speed.** Reference C3 = MIDI 60 = 1× (Live's numbering), one octave per 12 semitones, added to Bend. Last note wins, and the pitch holds after note-off, like a CV that stays where it was left. Note changes jump at once, so they're in tune from the first sample. Gate notes (C1–F1 by default) never set the pitch.
- **N11. Micro Break.** `breakOnMicro` off = Traverse, on = Silence.
  - **Traverse:** slice = floor(Break × Repeats). A change jumps there at once, keeping the position within the slice like a CD skipping, with a fade-out tail and a fade-in.
  - **Silence:** the last 90% × Break of each slice's playback is silent, with windowing fades at its edge.
- **N12. Reverse** starts each new slice at its end. A direction change fades like a jump.
- **N13. The slice envelope follows the head's position** in the slice, in playback direction, not the time since the slice began. With a speed change mid-slice (a new note), a time-based envelope fell silent until the next wrap. A position-based one stays right, and mid-slice jumps add a separate fade-in.
