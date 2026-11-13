#include "dsp/Modulation.h"

#include <algorithm>
#include <numbers>

namespace fault::dsp
{
double Modulator::rnd (int64_t cycle) const noexcept
{
    return (double) (mix64 (seed_ ^ mix64 ((uint64_t) cycle)) >> 11) * (1.0 / 9007199254740992.0);
}

float Modulator::shapeAt (const ModulatorParams& p, double cycles) const noexcept
{
    const double pos = cycles + (double) std::clamp (p.phase, 0.0f, 1.0f) + 1.0e-9;   // a grid line belongs to the new cycle
    const double cf = std::floor (pos), x = pos - cf;
    const auto cycle = (int64_t) cf;
    double v = 0.0;   // bipolar
    switch (p.shape)
    {
        case ModShape::sine: v = std::sin (2.0 * std::numbers::pi * x); break;
        case ModShape::triangle: v = x < 0.25 ? 4.0 * x : x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0; break;
        case ModShape::saw: v = 2.0 * x - 1.0; break;
        case ModShape::square: v = x < 0.5 ? 1.0 : -1.0; break;
        case ModShape::sampleHold: v = 2.0 * rnd (cycle) - 1.0; break;
        case ModShape::smoothRandom:
        {
            const double a = rnd (cycle), b = rnd (cycle + 1), w = 0.5 - 0.5 * std::cos (std::numbers::pi * x);
            v = 2.0 * (a + (b - a) * w) - 1.0;
            break;
        }
    }
    return (float) (p.bipolar ? v : 0.5 * (v + 1.0));
}

namespace
{
double syncBeats (const ModulatorParams& p) noexcept
{
    return kSyncDivisions[(size_t) std::clamp (p.sync, 0, (int) kSyncDivisions.size() - 1)].beats;
}
} // namespace

float Modulator::next (const ModulatorParams& p, const ModClock& c, double sr, int samples) noexcept
{
    const bool synced = p.sync >= 0;
    const double beats = synced ? syncBeats (p) : 1.0;
    if (synced && c.playing)
        cycles_ = c.ppq / beats;
    value_ = shapeAt (p, cycles_);
    // Advance. Synced and stopped: free-run at the host tempo.
    const double hz = synced ? std::max (1.0, c.bpm) / 60.0 / beats : std::clamp ((double) p.rateHz, 0.001, 100.0);
    if (! (synced && c.playing))
        cycles_ += hz * samples / sr;
    return value_;
}

float Modulator::at (const ModulatorParams& p, const ModClock& c) noexcept
{
    if (p.sync >= 0 && c.playing)
        value_ = shapeAt (p, c.ppq / syncBeats (p));
    return value_;
}

void EnvelopeFollower::setParams (const EnvParams& p) noexcept
{
    att_ = 1.0 - std::exp (-1.0 / (std::max (0.05, (double) p.attackMs) * 0.001 * sr_));
    rel_ = 1.0 - std::exp (-1.0 / (std::max (0.5, (double) p.releaseMs) * 0.001 * sr_));
    gain_ = std::pow (10.0, (double) p.gainDb / 20.0);
}

ModOffsets computeOffsets (const ModParams& p, const std::array<float, kNumModSources>& sources, bool micro) noexcept
{
    ModOffsets o;
    for (const auto& r : p.routes)
    {
        const int s = (int) r.source, d = (int) r.dest;
        if (s <= 0 || s >= kNumModSources || d <= 0 || d >= kNumModDests || r.depth == 0.0f)
            continue;
        o.d[(size_t) d] += std::clamp (r.depth, -1.0f, 1.0f) * sources[(size_t) s];
    }
    // The Bend attenuator doesn't apply in Micro mode (brief §3.10).
    if (! micro)
        o.d[(size_t) ModDest::bend] *= std::clamp (p.bendAtten, 0.0f, 1.0f);
    o.d[(size_t) ModDest::brk] *= std::clamp (p.breakAtten, 0.0f, 1.0f);
    o.d[(size_t) ModDest::corrupt] *= std::clamp (p.corruptAtten, 0.0f, 1.0f);
    return o;
}
} // namespace fault::dsp
