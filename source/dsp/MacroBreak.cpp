#include "dsp/MacroBreak.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fault::dsp
{
namespace
{
constexpr int kMaxPieces = 1024;

bool isCut (int option) noexcept { return option == breakHalve || option == breakSubdivide || option == breakAudioRate; }
} // namespace

BreakDecision decideBreak (const MacroTables& t, float knob, Pcg32& rng, const TickContext& c) noexcept
{
    BreakDecision d;
    float pos = 0.0f;
    const int z = MacroTables::zoneOf (t.breakZoneStart, knob, pos);
    if (z == 0)
        return d;
    const double chance = ((double) t.breakChanceLow[(size_t) z] + ((double) t.breakChanceHigh[(size_t) z] - t.breakChanceLow[(size_t) z]) * pos)
                          * rateScale (c.period / c.sampleRate, t.breakRateRefSeconds, t.breakRateExponent);
    if (! rng.chance (chance))
        return d;

    // What can work at this Time / Repeats / clock.
    const double L = c.sliceLength, minSlice = std::max (2.0, (double) t.breakMinSliceMs * 0.001 * c.sampleRate);
    std::array<float, kDivisors> divW {};
    bool anyDiv = false;
    for (int i = 0; i < kDivisors; ++i)
    {
        const double div = std::round ((double) t.breakDivisors[(size_t) i]);
        const float w = (c.synced ? t.breakDivWeightsSync : t.breakDivWeightsFree)[(size_t) i];
        divW[(size_t) i] = div >= 2.0 && L / div >= minSlice ? std::max (0.0f, w) : 0.0f;
        anyDiv = anyDiv || divW[(size_t) i] > 0.0f;
    }
    const double hzMax = std::max (t.audioRateHzMin, t.audioRateHzMax), hzLow = std::max ((double) t.audioRateHzMin, 2.0 * c.sampleRate / std::max (1.0, L));
    std::array<bool, kNumBreakOptions> feasible {};
    feasible[breakHalve] = L / 2.0 >= minSlice;
    feasible[breakJump] = c.repeats > 1;
    feasible[breakSubdivide] = anyDiv;
    feasible[breakAudioRate] = hzLow <= hzMax && L >= 4.0;
    feasible[breakSilence] = true;

    std::array<float, kNumBreakOptions> w {};
    for (int i = 0; i < kNumBreakOptions; ++i)
        w[(size_t) i] = feasible[(size_t) i] ? t.breakWeights[(size_t) (z * kNumBreakOptions + i)] : 0.0f;
    const int primary = pickWeighted (w.data(), kNumBreakOptions, rng);
    if (primary < 0)
        return d;

    // Everything zone: the other kinds (cut / jump / silence) may stack on the one picked.
    const double stack = t.breakStack[(size_t) z];
    int cut = isCut (primary) ? primary : -1;
    bool jump = primary == breakJump, silence = primary == breakSilence;
    if (stack > 0.0)
    {
        if (cut < 0 && rng.chance (stack))
        {
            std::array<float, kNumBreakOptions> cw {};
            for (int i : { (int) breakHalve, (int) breakSubdivide, (int) breakAudioRate }) cw[(size_t) i] = w[(size_t) i];
            cut = pickWeighted (cw.data(), kNumBreakOptions, rng);
        }
        if (! jump && rng.chance (stack)) jump = true;
        if (! silence && rng.chance (stack)) silence = true;
    }

    int sub = 0;
    switch (cut)
    {
        case breakHalve:
            d.pieces = 2;
            sub = rng.chance (t.breakSecondHalf) ? 1 : 0;
            break;
        case breakSubdivide:
        {
            const int i = pickWeighted (divW.data(), kDivisors, rng);
            d.pieces = i < 0 ? 1 : (int) std::round ((double) t.breakDivisors[(size_t) i]);
            sub = rng.chance (t.breakSubOffStart) ? rng.below (d.pieces) : 0;
            break;
        }
        case breakAudioRate:
        {
            const double hz = hzLow * std::pow (hzMax / hzLow, rng.uniform());   // log-uniform
            d.pieces = std::clamp ((int) std::lround (L * hz / c.sampleRate), 2, kMaxPieces);
            sub = rng.chance (t.breakSubOffStart) ? rng.below (d.pieces) : 0;   // mostly the transient: a buzz, not a hiss
            break;
        }
        default: break;
    }
    if (cut >= 0) d.mask |= 1u << cut;
    d.slice = sub;

    const int total = std::max (1, c.repeats) * d.pieces;
    if (jump && total > 1)
    {
        d.slice = rng.below (total);
        d.mask |= 1u << breakJump;
    }
    if (silence)
    {
        d.duty = std::clamp (rng.uniform (t.silenceMin, std::max (t.silenceMin, t.silenceMax)), 0.0, 0.9);
        d.mask |= 1u << breakSilence;
    }
    return d;
}
} // namespace fault::dsp
