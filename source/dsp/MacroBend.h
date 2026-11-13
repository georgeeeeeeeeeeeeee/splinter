#pragma once
// Macro Bend (brief §3.6): on each clock tick, one roll of the dice decides how the next window plays.
// The knob runs through cumulative zones (off, reverse, octave, two octaves, slew, tape stop,
// everything); each zone adds an option, and the position inside a zone raises the chance per tick that
// anything happens. All numbers come from MacroTables.

#include "dsp/MacroTables.h"
#include "dsp/Rng.h"

namespace fault::dsp
{
// What a tick needs to know to roll the dice (shared by Bend and Break).
struct TickContext
{
    double sampleRate = 48000.0;
    double period = 48000.0;        // tick period, samples
    double sliceLength = 48000.0;   // the knob's slice (window / Repeats), samples
    int repeats = 1;                // the Repeats knob
    bool synced = false;            // host or note clock
};

// Per-tick chance scaling: ticks faster than `ref` seconds get proportionally fewer events.
double rateScale (double periodSeconds, float refSeconds, float exponent) noexcept;
// Weighted pick over n weights (negatives count as 0); -1 if they're all zero.
int pickWeighted (const float* weights, int n, Pcg32& rng) noexcept;

struct BendDecision
{
    int option = -1;            // BendOption, or -1: plays normally (forward, 1x)
    double octaves = 0.0;       // target speed, octaves from 1x
    bool reverse = false;
    double glide = 0.0;         // slew time constant, samples (0 = jump straight to the speed)
    double stopLength = 0.0;    // tape stop: samples to reach a standstill (0 = none)
    float click = 0.0f;         // signed click level on a hard direction change (everything zone only)
    unsigned mask() const noexcept { return option < 0 ? 0u : (1u << option) | (reverse && option != bendReverse ? 1u : 0u); }
};

BendDecision decideBend (const MacroTables& t, float knob, Pcg32& rng, const TickContext& c) noexcept;
} // namespace fault::dsp
