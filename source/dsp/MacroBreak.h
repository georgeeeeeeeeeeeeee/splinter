#pragma once
// Macro Break (brief §3.6): on each clock tick, a roll of the dice decides how the window is cut up.
// Zones: off, halve (loop half a slice), jump (any slice), subdivide (extra repeats on top of the
// knob's), audio-rate repeats, silence, everything (options stack). The choices depend on Time,
// Repeats and the clock source: options that can't work at the current slice length are skipped, fast
// ticks get fewer events, and synced clocks favour straight divisions.

#include "dsp/MacroBend.h"

namespace fault::dsp
{
struct BreakDecision
{
    unsigned mask = 0;      // bit per BreakOption that fired (for the tick log / UI)
    int pieces = 1;         // the window is cut into repeats x pieces
    int slice = 0;          // which of those plays (0 .. repeats x pieces - 1)
    double duty = 0.0;      // silent fraction of each repeat
};

BreakDecision decideBreak (const MacroTables& t, float knob, Pcg32& rng, const TickContext& c) noexcept;
} // namespace fault::dsp
