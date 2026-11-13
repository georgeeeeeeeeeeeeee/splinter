#include "dsp/MacroBend.h"

#include <algorithm>
#include <cmath>

namespace fault::dsp
{
double rateScale (double periodSeconds, float refSeconds, float exponent) noexcept
{
    if (refSeconds <= 0.0f || periodSeconds >= (double) refSeconds)
        return 1.0;
    return std::pow (std::max (0.0, periodSeconds / (double) refSeconds), (double) std::max (0.0f, exponent));
}

int pickWeighted (const float* w, int n, Pcg32& rng) noexcept
{
    double total = 0.0;
    for (int i = 0; i < n; ++i) total += std::max (0.0, (double) w[i]);
    if (total <= 0.0)
        return -1;
    double r = rng.uniform() * total;
    for (int i = 0; i < n; ++i)
    {
        const double wi = std::max (0.0, (double) w[i]);
        if (wi > 0.0 && (r -= wi) < 0.0)
            return i;
    }
    for (int i = n - 1; i >= 0; --i)
        if (w[i] > 0) return i;
    return -1;
}

BendDecision decideBend (const MacroTables& t, float knob, Pcg32& rng, const TickContext& c) noexcept
{
    BendDecision d;
    float pos = 0.0f;
    const int z = MacroTables::zoneOf (t.bendZoneStart, knob, pos);
    if (z == 0)
        return d;
    const double chance = ((double) t.bendChanceLow[(size_t) z] + ((double) t.bendChanceHigh[(size_t) z] - t.bendChanceLow[(size_t) z]) * pos)
                          * rateScale (c.period / c.sampleRate, t.bendRateRefSeconds, t.bendRateExponent);
    if (! rng.chance (chance))
        return d;
    const float* w = &t.bendWeights[(size_t) (z * kNumBendOptions)];
    d.option = pickWeighted (w, kNumBendOptions, rng);
    if (d.option < 0)
        return d;
    const bool alsoReverse = rng.chance (t.bendAlsoReverse[(size_t) z]);
    const double up = rng.chance (t.bendUpChance) ? 1.0 : -1.0;
    switch (d.option)
    {
        case bendReverse: d.reverse = true; break;
        case bendOctave: d.octaves = up; d.reverse = alsoReverse; break;
        case bendTwoOctaves: d.octaves = 2.0 * up; d.reverse = alsoReverse; break;
        case bendSlew:
            d.octaves = rng.uniform (-1.0, 1.0) * t.slewRangeOctaves;
            d.glide = std::max (1.0, rng.uniform (t.slewTimeMin, std::max (t.slewTimeMin, t.slewTimeMax)) * c.period);
            d.reverse = alsoReverse;
            break;
        case bendTapeStop:
            d.stopLength = std::max (1.0, rng.uniform (t.tapeStopMin, std::max (t.tapeStopMin, t.tapeStopMax)) * c.period);
            d.reverse = alsoReverse;
            break;
        default: break;
    }
    if (z == kZones - 1 && t.clickLevel > 0.0f)
        d.click = (float) (t.clickLevel * rng.uniform (0.5, 1.0) * (rng.chance (0.5) ? 1.0 : -1.0));
    return d;
}
} // namespace fault::dsp
