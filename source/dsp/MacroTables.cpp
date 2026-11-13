#include "dsp/MacroTables.h"

#include <algorithm>
#include <type_traits>

namespace fault::dsp
{
static_assert (std::is_standard_layout_v<MacroTables>, "offsetof needs a standard-layout struct");

MacroTables MacroTables::defaults() noexcept
{
    MacroTables t;
#include "dsp/MacroTablesBaked.inc"
    return t;
}

#define FAULT_FIELD(name, columns, about) \
    MacroTables::Field { #name, offsetof (MacroTables, name), (int) (sizeof (MacroTables::name) / sizeof (float)), columns, about }

const MacroTables::Field* MacroTables::fields (int& count) noexcept
{
    static const Field list[] = {
        FAULT_FIELD (bendZoneStart, 1, "Bend: knob position (0-1) where each of the 7 zones begins. Zones: off, reverse, octave, two octaves, slew, tape stop, everything."),
        FAULT_FIELD (bendChanceLow, 1, "Bend: chance per tick that something happens, at the start of each zone."),
        FAULT_FIELD (bendChanceHigh, 1, "Bend: the same chance at the end of each zone (linear between)."),
        FAULT_FIELD (bendWeights, kNumBendOptions, "Bend: one row per zone, the weights for picking reverse, octave, two octaves, slew, tape stop when something happens."),
        FAULT_FIELD (bendAlsoReverse, 1, "Bend: per zone, the chance a pitch, slew or stop event also plays reversed."),
        FAULT_FIELD (bendUpChance, 1, "Bend: octave jumps go up with this chance, otherwise down."),
        FAULT_FIELD (slewRangeOctaves, 1, "Bend slew: the target speed is random within plus or minus this many octaves."),
        FAULT_FIELD (slewTimeMin, 1, "Bend slew: shortest glide time constant, as a fraction of the tick period."),
        FAULT_FIELD (slewTimeMax, 1, "Bend slew: longest glide time constant, as a fraction of the tick period."),
        FAULT_FIELD (tapeStopMin, 1, "Bend tape stop: shortest time to reach a standstill, as a fraction of the tick period."),
        FAULT_FIELD (tapeStopMax, 1, "Bend tape stop: longest time to reach a standstill, as a fraction of the tick period."),
        FAULT_FIELD (tapeStopCurve, 1, "Bend tape stop: speed follows (1 - t)^curve. Above 1 brakes hard at first, below 1 holds speed and stops late."),
        FAULT_FIELD (clickLevel, 1, "Bend everything zone: level of the vinyl click on a hard direction change (0 = none)."),
        FAULT_FIELD (clickMs, 1, "Bend everything zone: click length in milliseconds."),
        FAULT_FIELD (bendRateRefSeconds, 1, "Bend: ticks faster than this many seconds get fewer events..."),
        FAULT_FIELD (bendRateExponent, 1, "...the chance is multiplied by (period / ref) to this power (0 = no scaling)."),
        FAULT_FIELD (breakZoneStart, 1, "Break: knob position where each of the 7 zones begins. Zones: off, halve, jump, subdivide, audio rate, silence, everything."),
        FAULT_FIELD (breakChanceLow, 1, "Break: chance per tick that something happens, at the start of each zone."),
        FAULT_FIELD (breakChanceHigh, 1, "Break: the same chance at the end of each zone."),
        FAULT_FIELD (breakWeights, kNumBreakOptions, "Break: one row per zone, the weights for halve, jump, subdivide, audio rate, silence. Options that can't work at the current Time / Repeats are skipped."),
        FAULT_FIELD (breakSecondHalf, 1, "Break halve: chance of looping the second half of the slice rather than the first."),
        FAULT_FIELD (breakDivisors, 1, "Break subdivide: how many pieces a slice can be cut into (extra repeats on top of the Repeats knob)."),
        FAULT_FIELD (breakDivWeightsFree, 1, "Break subdivide: weight of each divisor on the internal clock."),
        FAULT_FIELD (breakDivWeightsSync, 1, "Break subdivide: weight of each divisor on the host or note clock."),
        FAULT_FIELD (breakSubOffStart, 1, "Break subdivide and audio rate: chance the looped piece is a random one rather than the slice's first (a ratchet on the transient)."),
        FAULT_FIELD (breakMinSliceMs, 1, "Break: halve and subdivide never make a slice shorter than this (ms)."),
        FAULT_FIELD (audioRateHzMin, 1, "Break audio rate: lowest slice repetition rate (Hz)."),
        FAULT_FIELD (audioRateHzMax, 1, "Break audio rate: highest slice repetition rate (Hz)."),
        FAULT_FIELD (silenceMin, 1, "Break silence: smallest fraction of each repeat that goes silent."),
        FAULT_FIELD (silenceMax, 1, "Break silence: largest fraction (the brief caps it at 0.9)."),
        FAULT_FIELD (breakStack, 1, "Break: per zone, the chance each other kind of option (cut, jump, silence) stacks on top of the one picked."),
        FAULT_FIELD (breakRateRefSeconds, 1, "Break: ticks faster than this many seconds get fewer events..."),
        FAULT_FIELD (breakRateExponent, 1, "...the chance is multiplied by (period / ref) to this power."),
    };
    count = (int) (sizeof (list) / sizeof (list[0]));
    return list;
}
#undef FAULT_FIELD

int MacroTables::zoneOf (const std::array<float, kZones>& starts, float knob, float& position) noexcept
{
    const float k = std::clamp (knob, 0.0f, 1.0f);
    int z = 0;
    for (int i = 1; i < kZones; ++i)
        if (k >= starts[(size_t) i])
            z = i;
    const float lo = starts[(size_t) z], hi = z + 1 < kZones ? std::max (lo, starts[(size_t) z + 1]) : 1.0f;
    position = hi > lo ? std::clamp ((k - lo) / (hi - lo), 0.0f, 1.0f) : 1.0f;
    return z;
}
} // namespace fault::dsp
