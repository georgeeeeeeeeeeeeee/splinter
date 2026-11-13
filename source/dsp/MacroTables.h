#pragma once
// Every number Macro mode's dice use: zone boundaries, chances, option weights and ranges (brief §3.6),
// in one plain struct so George can tune them.
//
// Dev builds load tuning/macro_tables.json over these and hot-reload it when it changes; the render CLI
// reads the same file. `fault-render --bake-tables tuning/macro_tables.json` writes the tuned values
// into MacroTablesBaked.inc, which is what release builds compile in.
//
// All fields are floats or float arrays, listed in fields() (name, place, size): the JSON reader, writer
// and baker are generic over that list, so adding a field means adding it here and in the list.

#include <array>
#include <cstddef>

namespace fault::dsp
{
// Macro Bend options (one is picked when a tick's dice say "something happens").
enum BendOption { bendReverse = 0, bendOctave, bendTwoOctaves, bendSlew, bendTapeStop, kNumBendOptions };
// Macro Break options.
enum BreakOption { breakHalve = 0, breakJump, breakSubdivide, breakAudioRate, breakSilence, kNumBreakOptions };

inline constexpr int kZones = 7;          // per knob: Off + 6 zones, each adding an option
inline constexpr int kDivisors = 6;

struct MacroTables
{
    // ---- Bend -------------------------------------------------------------------------------------
    std::array<float, kZones> bendZoneStart {};     // knob position where each zone begins (zone 0 = Off)
    std::array<float, kZones> bendChanceLow {};     // chance per tick that something happens, at the zone's start
    std::array<float, kZones> bendChanceHigh {};    // ... and at its end (linear between)
    std::array<float, kZones * kNumBendOptions> bendWeights {};   // per zone: reverse, octave, 2 octaves, slew, tape stop
    std::array<float, kZones> bendAlsoReverse {};   // chance a pitch / slew / stop event also plays reversed
    float bendUpChance = 0.5f;                      // octave jumps: chance of up rather than down
    float slewRangeOctaves = 2.0f;                  // slew target: random within ± this many octaves
    float slewTimeMin = 0.1f, slewTimeMax = 0.6f;   // glide time constant, as a fraction of the tick period
    float tapeStopMin = 0.25f, tapeStopMax = 0.9f;  // time to reach a stop, as a fraction of the tick period
    float tapeStopCurve = 1.4f;                     // speed = (1 - t)^curve: >1 brakes hard first, <1 late
    float clickLevel = 0.25f;                       // Everything zone: vinyl click on a hard direction change
    float clickMs = 2.0f;
    float bendRateRefSeconds = 0.25f;               // ticks faster than this get proportionally fewer events:
    float bendRateExponent = 0.5f;                  //   chance x (period / ref)^exponent

    // ---- Break ------------------------------------------------------------------------------------
    std::array<float, kZones> breakZoneStart {};
    std::array<float, kZones> breakChanceLow {};
    std::array<float, kZones> breakChanceHigh {};
    std::array<float, kZones * kNumBreakOptions> breakWeights {};   // per zone: halve, jump, subdivide, audio rate, silence
    float breakSecondHalf = 0.5f;                   // halve: chance of looping the second half, not the first
    std::array<float, kDivisors> breakDivisors {};  // subdivide: extra repeats on top of the knob's
    std::array<float, kDivisors> breakDivWeightsFree {};   // ... their weights on the internal clock
    std::array<float, kDivisors> breakDivWeightsSync {};   // ... and on host / note clock (straight ones favoured)
    float breakSubOffStart = 0.35f;                 // subdivide: chance the looped piece isn't the slice's first
    float breakMinSliceMs = 8.0f;                   // halve / subdivide never cut a slice shorter than this
    float audioRateHzMin = 40.0f, audioRateHzMax = 320.0f;   // audio-rate repeats: slice rate range
    float silenceMin = 0.2f, silenceMax = 0.9f;     // silence: fraction of each repeat that's silent
    std::array<float, kZones> breakStack {};        // chance each other kind of option stacks on (Everything)
    float breakRateRefSeconds = 0.25f;
    float breakRateExponent = 0.5f;

    static MacroTables defaults() noexcept;         // the baked values (MacroTablesBaked.inc)

    // Generic field list for the JSON reader / writer / baker.
    struct Field
    {
        const char* name;
        std::size_t offset;   // bytes into MacroTables
        int count;            // floats
        int columns;          // > 1: written as rows of this many (per-zone option weights)
        const char* about;
    };
    static const Field* fields (int& count) noexcept;
    float* at (const Field& f) noexcept { return reinterpret_cast<float*> (reinterpret_cast<char*> (this) + f.offset); }
    const float* at (const Field& f) const noexcept { return reinterpret_cast<const float*> (reinterpret_cast<const char*> (this) + f.offset); }

    // Which zone a knob position is in, and how far through it (0..1).
    static int zoneOf (const std::array<float, kZones>& starts, float knob, float& position) noexcept;
};
} // namespace fault::dsp
